#include "ThumbnailRenderer.h"
#include "../core/DarkModeUtils.h"
#include "../core/Document.h"
#include "../core/Page.h"
#include "../core/Paper.h"
#include "../layers/VectorLayer.h"
#include "../objects/InsertedObject.h"
#include "../pdf/PdfProvider.h"

#include <algorithm>
#include <cmath>
#include <QPainter>
#include <QtConcurrent>
#include <QThreadStorage>
#include <QDebug>

// Thread-local cached PDF provider (keyed by resolved source path) so worker
// threads never touch the Document's lazy provider map. Mirrors the same pattern
// used by DocumentViewport's async PDF preload.
namespace {
struct ThumbPdfCache {
    QString pdfPath;
    std::unique_ptr<PdfProvider> provider;
    
    PdfProvider* getOrCreate(const QString& path) {
        if (pdfPath != path || !provider || !provider->isValid()) {
            pdfPath = path;
            provider = PdfProvider::create(path);
        }
        return provider.get();
    }
};
QThreadStorage<ThumbPdfCache> s_thumbPdfCache;
}

ThumbnailRenderer::ThumbnailRenderer(QObject* parent)
    : QObject(parent)
{
}

ThumbnailRenderer::~ThumbnailRenderer()
{
    m_shuttingDown = true;
    cancelAll();
}

void ThumbnailRenderer::requestThumbnail(Document* doc, int pageIndex, int width, qreal dpr)
{
    if (!doc || pageIndex < 0 || pageIndex >= doc->pageCount() || width <= 0) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // Check if already pending or active
    if (m_activePages.contains(pageIndex)) {
        return;
    }
    
    for (const ThumbnailRequest& req : m_pendingRequests) {
        if (req.pageIndex == pageIndex) {
            return;
        }
    }
    
    // Drop oldest requests if queue is full
    while (m_pendingRequests.size() >= MAX_PENDING_REQUESTS) {
        m_pendingRequests.removeFirst();
    }
    
    m_pendingRequests.append({doc, pageIndex, width, dpr, m_pdfDarkMode});
    
    locker.unlock();
    
    startNextTask();
}

void ThumbnailRenderer::setPdfDarkMode(bool enabled)
{
    m_pdfDarkMode = enabled;
}

void ThumbnailRenderer::cancelAll()
{
    QMutexLocker locker(&m_mutex);
    
    // Clear lightweight pending requests (trivially cheap)
    m_pendingRequests.clear();
    
    // Cancel active watchers
    for (QFutureWatcher<QPair<int, QPixmap>>* watcher : m_activeWatchers) {
        watcher->cancel();
        watcher->waitForFinished();
        delete watcher;
    }
    m_activeWatchers.clear();
    m_activePages.clear();
}

bool ThumbnailRenderer::isPending(int pageIndex) const
{
    QMutexLocker locker(&m_mutex);
    
    if (m_activePages.contains(pageIndex)) {
        return true;
    }
    
    for (const ThumbnailRequest& req : m_pendingRequests) {
        if (req.pageIndex == pageIndex) {
            return true;
        }
    }
    
    return false;
}

void ThumbnailRenderer::setMaxConcurrentRenders(int max)
{
    QMutexLocker locker(&m_mutex);
    m_maxConcurrent = qMax(1, max);
}

void ThumbnailRenderer::startNextTask()
{
    QMutexLocker locker(&m_mutex);
    
    while (m_activeWatchers.size() < m_maxConcurrent && !m_pendingRequests.isEmpty()) {
        ThumbnailRequest req = m_pendingRequests.takeFirst();
        
        // Unlock while creating the heavy snapshot on the main thread
        locker.unlock();
        
        bool wasLoaded = req.doc->isPageLoaded(req.pageIndex);
        
        ThumbnailSnapshot snapshot = createSnapshot(
            req.doc, req.pageIndex, req.width, req.dpr,
            req.pdfDarkMode);
        
        // Evict pages loaded only for thumbnail rendering to prevent
        // m_loadedPages from growing unboundedly during fast panel scrolling
        if (!wasLoaded && req.doc->isLazyLoadEnabled()
            && req.doc->isPageLoaded(req.pageIndex)) {
            req.doc->evictPage(req.pageIndex);
        }
        
        locker.relock();
        
        if (!snapshot.valid) {
            continue;
        }
        
        int pageIndex = snapshot.pageIndex;
        m_activePages.insert(pageIndex);
        
        auto* watcher = new QFutureWatcher<QPair<int, QPixmap>>(this);
        connect(watcher, &QFutureWatcher<QPair<int, QPixmap>>::finished,
                this, &ThumbnailRenderer::onRenderFinished);
        
        m_activeWatchers.append(watcher);
        
        QFuture<QPair<int, QPixmap>> future = QtConcurrent::run([snapshot = std::move(snapshot)]() {
            QPixmap result = renderFromSnapshot(snapshot);
            return qMakePair(snapshot.pageIndex, result);
        });
        
        watcher->setFuture(future);
    }
}

void ThumbnailRenderer::onRenderFinished()
{
    if (m_shuttingDown) {
        return;
    }
    
    // QFutureWatcher is a template without Q_OBJECT, so use static_cast
    auto* watcher = static_cast<QFutureWatcher<QPair<int, QPixmap>>*>(sender());
    if (!watcher) {
        return;
    }
    
    // Get result before we lock the mutex
    QPair<int, QPixmap> result;
    bool wasCancelled = watcher->isCanceled();
    if (!wasCancelled) {
        result = watcher->result();
    }
    
    {
        QMutexLocker locker(&m_mutex);
        
        // Remove from active
        m_activeWatchers.removeOne(watcher);
        if (!wasCancelled) {
            m_activePages.remove(result.first);
        }
    }
    
    // Delete watcher immediately to free the QFuture's result QPixmap.
    // Safe per Qt docs: "always safe to delete a QObject from within a slot
    // connected to that object's own signal." The watcher has already been
    // removed from m_activeWatchers and the result extracted above.
    delete watcher;
    
    // Emit result if not cancelled
    if (!wasCancelled && !result.second.isNull()) {
        emit thumbnailReady(result.first, result.second);
    }
    
    // Try to start next task
    startNextTask();
}

ThumbnailRenderer::ThumbnailSnapshot ThumbnailRenderer::createSnapshot(
    Document* doc, int pageIndex, int width, qreal dpr, bool pdfDarkMode)
{
    ThumbnailSnapshot snapshot;
    snapshot.pageIndex = pageIndex;
    snapshot.width = width;
    snapshot.dpr = dpr;
    
    // Get page size from metadata (doesn't trigger lazy load)
    QSizeF pageSize = doc->pageSizeAt(pageIndex);
    if (pageSize.isEmpty()) {
        pageSize = QSizeF(612, 792);  // Default US Letter
    }
    snapshot.pageSize = pageSize;
    
    // Try to get the page (may trigger lazy load)
    // This is safe because we're on the main thread
    Page* page = doc->page(pageIndex);
    if (!page) {
        return snapshot;
    }
    
    // Copy background settings
    snapshot.backgroundType = page->backgroundType;
    snapshot.backgroundColor = page->backgroundColor;
    snapshot.gridColor = page->gridColor;
    snapshot.gridSpacing = page->gridSpacing;
    snapshot.lineSpacing = page->lineSpacing;
    snapshot.paperId = page->paperId;
    if (page->backgroundType == Page::BackgroundType::Custom && !page->customImage.isEmpty()
        && !doc->bundlePath().isEmpty()) {
        snapshot.customImagePath = doc->bundlePath() + QStringLiteral("/assets/images/") + page->customImage;
    }

    // Calculate thumbnail dimensions
    qreal aspectRatio = pageSize.height() / pageSize.width();
    int thumbnailWidth = width;
    int thumbnailHeight = static_cast<int>(width * aspectRatio);
    
    // Store PDF info for deferred rendering in the worker thread.
    // MuPdfProvider::renderPageToImage() is already mutex-protected,
    // so calling it from a worker is safe and keeps the main thread responsive.
    if (page->backgroundType == Page::BackgroundType::PDF && page->pdfPageNumber >= 0
        && doc->providerForSource(page->pdfSourceId)) {
        // The worker renders directly against pdfSourcePath (the bundled mini-PDF when
        // the source is bundled), so store the provider-facing index (pageMap-resolved).
        const int renderPageNum = doc->resolveSourcePageIndex(page->pdfSourceId, page->pdfPageNumber);
        if (renderPageNum >= 0) {
            snapshot.doc = doc;
            snapshot.pdfPageNumber = renderPageNum;
            snapshot.pdfSourceId = page->pdfSourceId;
            snapshot.pdfSourcePath = doc->pdfPathForSource(page->pdfSourceId);
            snapshot.pdfDarkMode = pdfDarkMode;
            qreal pdfDpi = (thumbnailWidth * dpr) / (pageSize.width() / 72.0);
            snapshot.pdfDpi = qMin(pdfDpi, 96.0);
        }
    }
    
    // Deep copy stroke data from all layers
    for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        if (layer) {
            LayerSnapshot layerSnap;
            layerSnap.visible = layer->visible;
            layerSnap.opacity = layer->opacity;
            layerSnap.strokes = layer->strokes();
            snapshot.layers.append(std::move(layerSnap));
        }
    }
    
    // Pre-render objects to a pixmap on main thread
    // Objects may contain QPixmap data that isn't safe to copy across threads
    if (page->objectCount() > 0 && pageSize.width() > 0 && pageSize.height() > 0) {
        int physicalWidth = static_cast<int>(thumbnailWidth * dpr);
        int physicalHeight = static_cast<int>(thumbnailHeight * dpr);
        
        if (physicalWidth > 0 && physicalHeight > 0) {
            snapshot.objectsLayer = QPixmap(physicalWidth, physicalHeight);
            
            if (!snapshot.objectsLayer.isNull()) {
                snapshot.hasObjects = true;
                snapshot.objectsLayer.setDevicePixelRatio(dpr);
                snapshot.objectsLayer.fill(Qt::transparent);
                
                QPainter objPainter(&snapshot.objectsLayer);
                if (objPainter.isActive()) {
                    objPainter.setRenderHint(QPainter::Antialiasing, true);
                    objPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    
                    qreal scaleX = static_cast<qreal>(thumbnailWidth) / pageSize.width();
                    qreal scaleY = static_cast<qreal>(thumbnailHeight) / pageSize.height();
                    qreal scale = qMin(scaleX, scaleY);
                    objPainter.scale(scale, scale);
                    
                    page->renderObjects(objPainter, 1.0);
                    objPainter.end();
                }
            }
        }
    }
    
    snapshot.valid = true;
    return snapshot;
}

QPixmap ThumbnailRenderer::renderFromSnapshot(const ThumbnailSnapshot& snapshot)
{
    if (!snapshot.valid) {
        return QPixmap();
    }
    
    int width = snapshot.width;
    qreal dpr = snapshot.dpr;
    QSizeF pageSize = snapshot.pageSize;
    
    if (pageSize.width() <= 0 || pageSize.height() <= 0) {
        return QPixmap();
    }
    
    // Calculate thumbnail dimensions
    qreal aspectRatio = pageSize.height() / pageSize.width();
    int thumbnailWidth = width;
    int thumbnailHeight = static_cast<int>(width * aspectRatio);
    
    int physicalWidth = static_cast<int>(thumbnailWidth * dpr);
    int physicalHeight = static_cast<int>(thumbnailHeight * dpr);
    
    if (physicalWidth <= 0 || physicalHeight <= 0) {
        return QPixmap();
    }
    
    // Render PDF background in the worker thread (deferred from createSnapshot)
    QPixmap pdfBackground;
    if (snapshot.pdfPageNumber >= 0 && !snapshot.pdfSourcePath.isEmpty() && snapshot.pdfDpi > 0) {
        // Render via a thread-local provider keyed by the resolved source path so the
        // worker never touches the Document's provider map from a non-main thread.
        ThumbPdfCache& cache = s_thumbPdfCache.localData();
        PdfProvider* threadPdf = cache.getOrCreate(snapshot.pdfSourcePath);
        if (threadPdf && threadPdf->isValid()) {
            QImage pdfImage = threadPdf->renderPageToImage(snapshot.pdfPageNumber, snapshot.pdfDpi);
            if (!pdfImage.isNull()) {
                if (snapshot.pdfDarkMode) {
                    QVector<QRect> imgRegions = threadPdf->imageRegions(
                        snapshot.pdfPageNumber, snapshot.pdfDpi);
                    DarkModeUtils::invertImageLightness(pdfImage, imgRegions);
                }
                pdfBackground = QPixmap::fromImage(pdfImage);
            }
            threadPdf->trimStore();
        }
    }
    
    QPixmap thumbnail(physicalWidth, physicalHeight);
    if (thumbnail.isNull()) {
        return QPixmap();
    }
    thumbnail.setDevicePixelRatio(dpr);
    thumbnail.fill(Qt::white);
    
    QPainter painter(&thumbnail);
    if (!painter.isActive()) {
        return QPixmap();
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    qreal scaleX = static_cast<qreal>(thumbnailWidth) / pageSize.width();
    qreal scaleY = static_cast<qreal>(thumbnailHeight) / pageSize.height();
    qreal scale = qMin(scaleX, scaleY);
    
    painter.scale(scale, scale);
    
    // 1. Render background
    QRectF pageRect(0, 0, pageSize.width(), pageSize.height());
    
    if (!pdfBackground.isNull()) {
        painter.drawPixmap(pageRect.toRect(), pdfBackground);
    } else if (!snapshot.customImagePath.isEmpty()) {
        painter.fillRect(pageRect, snapshot.backgroundColor);
        const QImage paper = Paper::userPaperImage(snapshot.customImagePath);
        if (!paper.isNull()) painter.drawImage(pageRect, paper);
    } else {
        Page::renderBackgroundPattern(
            painter,
            pageRect,
            snapshot.backgroundColor,
            snapshot.backgroundType,
            snapshot.gridColor,
            snapshot.gridSpacing,
            snapshot.lineSpacing,
            1.0,
            snapshot.paperId
        );
    }
    
    // 2. Render vector layers from snapshot (thread-safe - all data is local)
    for (const LayerSnapshot& layerSnap : snapshot.layers) {
        if (!layerSnap.visible) {
            continue;
        }
        
        painter.save();
        if (layerSnap.opacity < 1.0) {
            painter.setOpacity(layerSnap.opacity);
        }
        
        for (const VectorStroke& stroke : layerSnap.strokes) {
            VectorLayer::renderStroke(painter, stroke);
        }
        
        painter.restore();
    }
    
    // 3. Render pre-rendered objects layer
    if (snapshot.hasObjects && !snapshot.objectsLayer.isNull()) {
        painter.resetTransform();
        painter.drawPixmap(0, 0, snapshot.objectsLayer);
    }
    
    painter.end();
    return thumbnail;
}


// ============================================================================
// The first page of a note, for the start screen and the Library (moved from
// MainWindow, Step N4). GUI thread: it reads the live Document.
// ============================================================================

namespace {

QPixmap renderPagedThumbnail(Document* doc, int width, qreal dpr)
{
    // Phase P.4.6: Render page-0 thumbnail for saving to NotebookLibrary
    if (!doc || doc->isEdgeless() || doc->pageCount() == 0) {
        return QPixmap();
    }
    
    
    // Get page size from metadata
    QSizeF pageSize = doc->pageSizeAt(0);
    if (pageSize.isEmpty()) {
        pageSize = QSizeF(612, 792);  // Default US Letter
    }
    
    // Calculate dimensions
    qreal aspectRatio = pageSize.height() / pageSize.width();
    int thumbnailHeight = static_cast<int>(width * aspectRatio);
    
    int physicalWidth = static_cast<int>(width * dpr);
    int physicalHeight = static_cast<int>(thumbnailHeight * dpr);
    
    // Create pixmap
    QPixmap thumbnail(physicalWidth, physicalHeight);
    thumbnail.setDevicePixelRatio(dpr);
    thumbnail.fill(Qt::white);
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Calculate scale factor
    qreal scale = static_cast<qreal>(width) / pageSize.width();
    painter.scale(scale, scale);
    
    // Get the page (may trigger lazy load)
    Page* page = doc->page(0);
    if (!page) {
        qWarning() << "renderPage0Thumbnail: page(0) returned nullptr";
        painter.end();
        return thumbnail;  // Return white placeholder
    }
    
    // Defensive check: verify page has layers (should always have at least 1)
    int layerCount = page->layerCount();
    if (layerCount <= 0) {
        qWarning() << "renderPage0Thumbnail: page has no layers, skipping layer rendering";
    }
    
    // Render PDF background if available (resolve the page's own PDF source)
    QPixmap pdfBackground;
    if (page->backgroundType == Page::BackgroundType::PDF && page->pdfPageNumber >= 0
        && doc->providerForSource(page->pdfSourceId)) {
        qreal pdfDpi = (width * dpr) / (pageSize.width() / 72.0);
        pdfDpi = qMin(pdfDpi, 150.0);  // Cap at 150 DPI

        QImage pdfImage = doc->renderPdfPageToImage(page->pdfSourceId, page->pdfPageNumber, pdfDpi);
        if (!pdfImage.isNull()) {
            pdfBackground = QPixmap::fromImage(pdfImage);
        }
    }
    
    // Render background
    page->renderBackground(painter, pdfBackground.isNull() ? nullptr : &pdfBackground, 1.0);
    
    // Render vector layers (with bounds check)
    for (int layerIdx = 0; layerIdx < layerCount; ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        if (layer && layer->visible) {
            layer->render(painter);
        }
    }
    
    // Render inserted objects
    page->renderObjects(painter, 1.0);
    
    painter.end();
    return thumbnail;
}

QPixmap renderEdgelessThumbnail(Document* doc, int width, qreal dpr)
{
    if (!doc || !doc->isEdgeless()) {
        return QPixmap();
    }
    
    
    int physicalWidth = static_cast<int>(width * dpr);
    int physicalHeight = static_cast<int>(width * dpr);
    
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    // Determine the center point for the thumbnail viewport.
    // Primary: last_position (where the user was last looking).
    // Fallback: center of the first indexed tile if last_position has no nearby tiles.
    QPointF center = doc->edgelessLastPosition();
    
    // Define a virtual viewport: 1 tile centered on the position.
    // A single tile keeps content legible at small thumbnail sizes.
    qreal viewExtent = tileSize * 1.0;
    QRectF viewRect(center.x() - viewExtent / 2.0, center.y() - viewExtent / 2.0,
                    viewExtent, viewExtent);
    
    // Find tiles that intersect this viewport (with margin for strokes at edges).
    // tilesInRect() returns ALL coordinate positions in the range, even empty ones,
    // so we filter to keep only coordinates where an actual tile exists.
    static constexpr int STROKE_MARGIN = 100;
    QRectF marginRect = viewRect.adjusted(-STROKE_MARGIN, -STROKE_MARGIN,
                                          STROKE_MARGIN, STROKE_MARGIN);
    QVector<Document::TileCoord> allTiles;
    {
        QVector<Document::TileCoord> candidates = doc->tilesInRect(marginRect);
        for (const auto& coord : candidates) {
            if (doc->getTile(coord.first, coord.second)) {
                allTiles.append(coord);
            }
        }
    }
    
    // Fallback: if no tiles with content near last_position, try the first loaded tile
    if (allTiles.isEmpty()) {
        QVector<Document::TileCoord> allCoords = doc->allTileCoords();
        if (!allCoords.isEmpty()) {
            // Use first tile and re-center viewport on it
            auto firstCoord = allCoords.first();
            center = QPointF(firstCoord.first * tileSize + tileSize / 2.0,
                             firstCoord.second * tileSize + tileSize / 2.0);
            viewRect = QRectF(center.x() - viewExtent / 2.0, center.y() - viewExtent / 2.0,
                              viewExtent, viewExtent);
            marginRect = viewRect.adjusted(-STROKE_MARGIN, -STROKE_MARGIN,
                                           STROKE_MARGIN, STROKE_MARGIN);
            QVector<Document::TileCoord> fallbackCandidates = doc->tilesInRect(marginRect);
            for (const auto& coord : fallbackCandidates) {
                if (doc->getTile(coord.first, coord.second)) {
                    allTiles.append(coord);
                }
            }
        }
    }
    
    // Create the thumbnail pixmap
    QPixmap thumbnail(physicalWidth, physicalHeight);
    thumbnail.setDevicePixelRatio(dpr);
    thumbnail.fill(doc->defaultBackgroundColor);
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Calculate scale: map viewExtent doc units to width logical pixels
    qreal scale = static_cast<qreal>(width) / viewExtent;
    
    // Transform: translate so viewRect.topLeft maps to (0,0), then scale
    painter.scale(scale, scale);
    painter.translate(-viewRect.left(), -viewRect.top());
    
    // Pre-calculate visible tile range for background pass
    int minVisibleTx = static_cast<int>(std::floor(viewRect.left() / tileSize));
    int maxVisibleTx = static_cast<int>(std::floor(viewRect.right() / tileSize));
    int minVisibleTy = static_cast<int>(std::floor(viewRect.top() / tileSize));
    int maxVisibleTy = static_cast<int>(std::floor(viewRect.bottom() / tileSize));
    
    // ===== PASS 1: Render backgrounds for visible tiles =====
    // Render all tile positions in the view, even empty ones (they get default background)
    for (int tx = minVisibleTx; tx <= maxVisibleTx; ++tx) {
        for (int ty = minVisibleTy; ty <= maxVisibleTy; ++ty) {
            QRectF tileRect(tx * tileSize, ty * tileSize, tileSize, tileSize);
            Page* tile = doc->getTile(tx, ty);
            
            if (tile) {
                Page::renderBackgroundPattern(
                    painter, tileRect,
                    tile->backgroundColor, tile->backgroundType,
                    tile->gridColor, tile->gridSpacing, tile->lineSpacing,
                    1.0,  // No zoom compensation needed for static thumbnail
                    tile->paperId, true
                );
            } else {
                Page::renderBackgroundPattern(
                    painter, tileRect,
                    doc->defaultBackgroundColor, doc->defaultBackgroundType,
                    doc->defaultGridColor, doc->defaultGridSpacing, doc->defaultLineSpacing,
                    1.0, doc->defaultPaperId, true
                );
            }
        }
    }
    
    // If no tiles with content exist, we're done (thumbnail shows just the background)
    if (allTiles.isEmpty()) {
        painter.end();
        return thumbnail;
    }
    
    // ===== PASS 2: Render objects with default affinity (-1) =====
    // These render below all stroke layers
    const auto& layers = doc->edgelessLayers();
    auto renderObjectsWithAffinity = [&](int affinity) {
        // Check if the tied layer is visible (affinity K ties to Layer K+1)
        int tiedLayerIndex = affinity + 1;
        if (tiedLayerIndex >= 0 && tiedLayerIndex < static_cast<int>(layers.size())) {
            if (!layers[tiedLayerIndex].visible) return;
        }
        
        for (const auto& coord : allTiles) {
            Page* tile = doc->getTile(coord.first, coord.second);
            if (!tile) continue;
            
            auto it = tile->objectsByAffinity.find(affinity);
            if (it == tile->objectsByAffinity.end() || it->second.empty()) continue;
            
            QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
            
            // Sort by z-order
            std::vector<InsertedObject*> objs = it->second;
            std::sort(objs.begin(), objs.end(),
                      [](InsertedObject* a, InsertedObject* b) {
                          return a->zOrder < b->zOrder;
                      });
            
            for (InsertedObject* obj : objs) {
                if (!obj->visible) continue;
                painter.save();
                painter.translate(tileOrigin);
                obj->render(painter, 1.0);
                painter.restore();
            }
        }
    };
    
    renderObjectsWithAffinity(-1);
    
    // ===== PASS 3: Interleaved layer strokes and objects =====
    int maxLayerCount = 0;
    for (const auto& coord : allTiles) {
        Page* tile = doc->getTile(coord.first, coord.second);
        if (tile) {
            maxLayerCount = qMax(maxLayerCount, tile->layerCount());
        }
    }
    
    for (int layerIdx = 0; layerIdx < maxLayerCount; ++layerIdx) {
        // PASS 3a: Render this layer's strokes from all tiles
        for (const auto& coord : allTiles) {
            Page* tile = doc->getTile(coord.first, coord.second);
            if (!tile) continue;
            if (layerIdx >= tile->layerCount()) continue;
            
            VectorLayer* layer = tile->layer(layerIdx);
            if (!layer || !layer->visible) continue;
            
            QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
            painter.save();
            painter.translate(tileOrigin);
            layer->render(painter);
            painter.restore();
        }
        
        // PASS 3b: Render objects with affinity = layerIdx
        renderObjectsWithAffinity(layerIdx);
    }
    
    painter.end();
    return thumbnail;
}

} // namespace

QPixmap ThumbnailRenderer::renderFirstPage(Document* doc, int width, qreal dpr)
{
    if (!doc || width <= 0) return QPixmap();
    return doc->isEdgeless() ? renderEdgelessThumbnail(doc, width, dpr) : renderPagedThumbnail(doc, width, dpr);
}
