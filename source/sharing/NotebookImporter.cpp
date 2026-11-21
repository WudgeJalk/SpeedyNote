#include "NotebookImporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

// miniz - cross-platform ZIP library (MIT license)
#include "miniz.h"

// ============================================================================
// NotebookImporter Implementation
// ============================================================================

NotebookImporter::ImportResult NotebookImporter::importPackage(
    const QString& snbxPath, 
    const QString& destDir)
{
    ImportResult result;
    
    // Validate inputs
    if (snbxPath.isEmpty()) {
        result.errorMessage = QObject::tr("No package file specified");
        return result;
    }
    
    if (!QFile::exists(snbxPath)) {
        result.errorMessage = QObject::tr("Package file not found: %1").arg(snbxPath);
        return result;
    }
    
    if (destDir.isEmpty()) {
        result.errorMessage = QObject::tr("No destination directory specified");
        return result;
    }
    
    // Create destination directory if it doesn't exist
    QDir destDirObj(destDir);
    if (!destDirObj.exists() && !destDirObj.mkpath(".")) {
        result.errorMessage = QObject::tr("Failed to create destination directory: %1").arg(destDir);
        return result;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "NotebookImporter: Importing" << snbxPath << "to" << destDir;
    #endif
    
    // Open the ZIP archive
    mz_zip_archive zipArchive;
    memset(&zipArchive, 0, sizeof(zipArchive));
    
    QByteArray snbxPathUtf8 = snbxPath.toUtf8();
    if (!mz_zip_reader_init_file(&zipArchive, snbxPathUtf8.constData(), 0)) {
        result.errorMessage = QObject::tr("Failed to open package file: %1").arg(snbxPath);
        return result;
    }
    
    // Helper lambda to clean up on error
    auto cleanup = [&]() {
        mz_zip_reader_end(&zipArchive);
    };
    
    // Find the .snb folder name inside the ZIP
    // Expected structure: NotebookName.snb/document.json, NotebookName.snb/pages/, etc.
    QString snbFolderName;
    int numFiles = static_cast<int>(mz_zip_reader_get_num_files(&zipArchive));
    
    for (int i = 0; i < numFiles; i++) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zipArchive, i, &fileStat)) {
            continue;
        }
        
        QString entryName = QString::fromUtf8(fileStat.m_filename);
        
        // Look for a .snb folder (ends with .snb/)
        if (entryName.contains(".snb/")) {
            qsizetype snbIndex = entryName.indexOf(".snb/");
            snbFolderName = entryName.left(snbIndex + 4);  // Include ".snb"
            break;
        }
    }
    
    if (snbFolderName.isEmpty()) {
        result.errorMessage = QObject::tr("Invalid package: no .snb folder found");
        cleanup();
        return result;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "NotebookImporter: Found notebook folder:" << snbFolderName;
    #endif
    
    // Resolve name conflicts (auto-rename if necessary)
    QString finalSnbName = resolveNameConflict(snbFolderName, destDir);
    QString extractedSnbPath = destDir + "/" + finalSnbName;
    
    #ifdef SPEEDYNOTE_DEBUG
    if (finalSnbName != snbFolderName) {
        qDebug() << "NotebookImporter: Renamed to avoid conflict:" << finalSnbName;
    }
    #endif
    
    // Create the .snb folder
    QDir snbDir(extractedSnbPath);
    if (!snbDir.mkpath(".")) {
        result.errorMessage = QObject::tr("Failed to create notebook folder: %1").arg(extractedSnbPath);
        cleanup();
        return result;
    }
    
    // Track if we find an embedded PDF
    QString embeddedPdfPath;
    
    // Compute per-notebook embedded folder to avoid PDF name collisions
    // e.g., "MyNotebook.snb" -> "embedded/MyNotebook_..."
    // We use "embedded/" as the top-level folder but include the notebook name
    QString embeddedFolderPath = destDir + "/embedded";
    
    // Extract all files
    for (int i = 0; i < numFiles; i++) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zipArchive, i, &fileStat)) {
            qWarning() << "NotebookImporter: Failed to get file stat for index" << i;
            continue;
        }
        
        QString entryName = QString::fromUtf8(fileStat.m_filename);
        
        // Skip directory entries
        if (fileStat.m_is_directory) {
            continue;
        }
        
        // Determine where to extract this file
        QString extractPath;
        
        if (entryName.startsWith(snbFolderName + "/") || entryName.startsWith(snbFolderName)) {
            // File inside the .snb folder
            QString relativePath = entryName.mid(snbFolderName.length());
            if (relativePath.startsWith("/")) {
                relativePath = relativePath.mid(1);
            }
            
            if (relativePath.isEmpty()) {
                continue;  // Skip the folder entry itself
            }
            
            extractPath = extractedSnbPath + "/" + relativePath;
        } else if (entryName.startsWith("embedded/")) {
            // Embedded PDF or other file
            // Extract to per-notebook folder: destDir/embedded/NotebookName_filename.pdf
            // This avoids collision when two packages have PDFs with the same filename
            QString embeddedFileName = entryName.mid(9);  // Remove "embedded/"
            
            // Create unique name: NotebookName_originalFileName.pdf
            QString notebookBaseName = finalSnbName;
            if (notebookBaseName.endsWith(".snb", Qt::CaseInsensitive)) {
                notebookBaseName = notebookBaseName.left(notebookBaseName.length() - 4);
            }
            QString uniqueFileName = notebookBaseName + "_" + embeddedFileName;
            
            extractPath = embeddedFolderPath + "/" + uniqueFileName;
            
            // Track PDF path
            if (entryName.endsWith(".pdf", Qt::CaseInsensitive)) {
                embeddedPdfPath = extractPath;
            }
        } else {
            // Unknown file - skip
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "NotebookImporter: Skipping unknown entry:" << entryName;
            #endif
            continue;
        }
        
        // Create parent directory for the file
        QFileInfo extractInfo(extractPath);
        QDir extractDir = extractInfo.absoluteDir();
        if (!extractDir.exists() && !extractDir.mkpath(".")) {
            qWarning() << "NotebookImporter: Failed to create directory:" << extractDir.absolutePath();
            continue;
        }
        
        // Extract the file
        size_t uncompressedSize = static_cast<size_t>(fileStat.m_uncomp_size);
        
        // Allocate buffer for file contents
        void* fileData = malloc(uncompressedSize);
        if (!fileData) {
            qWarning() << "NotebookImporter: Failed to allocate memory for:" << entryName;
            continue;
        }
        
        if (!mz_zip_reader_extract_to_mem(&zipArchive, i, fileData, uncompressedSize, 0)) {
            qWarning() << "NotebookImporter: Failed to extract:" << entryName;
            free(fileData);
            continue;
        }
        
        // Write to file
        QFile outFile(extractPath);
        if (!outFile.open(QIODevice::WriteOnly)) {
            qWarning() << "NotebookImporter: Failed to write:" << extractPath;
            free(fileData);
            continue;
        }
        
        outFile.write(static_cast<const char*>(fileData), static_cast<qint64>(uncompressedSize));
        outFile.close();
        free(fileData);
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "NotebookImporter: Extracted:" << extractPath;
        #endif
    }
    
    // Close the ZIP archive
    mz_zip_reader_end(&zipArchive);
    
    // Verify the extraction by checking for document.json
    QString manifestPath = extractedSnbPath + "/document.json";
    if (!QFile::exists(manifestPath)) {
        // Extraction failed - clean up .snb folder
        QDir(extractedSnbPath).removeRecursively();
        
        // Also clean up embedded PDF if extracted
        if (!embeddedPdfPath.isEmpty()) {
            QFile::remove(embeddedPdfPath);
        }
        
        // Try to remove embedded/ folder if empty (only clean up what we created)
        QDir embeddedDir(embeddedFolderPath);
        if (embeddedDir.exists()) {
            // Remove only if empty (other notebooks may have files there)
            embeddedDir.rmdir(".");  // Only succeeds if empty
        }
        
        result.errorMessage = QObject::tr("Invalid package: document.json not found after extraction");
        return result;
    }
    
    // If we extracted an embedded PDF, update document.json to point to the renamed file
    // The exporter writes pdf_relative_path = "../embedded/filename.pdf" but we renamed
    // the file to "NotebookName_filename.pdf" to avoid collisions
    if (!embeddedPdfPath.isEmpty()) {
        QFile manifestFile(manifestPath);
        if (manifestFile.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError;
            QJsonDocument jsonDoc = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
            manifestFile.close();
            
            if (!jsonDoc.isNull() && jsonDoc.isObject()) {
                QJsonObject root = jsonDoc.object();
                
                // Calculate new relative path from document.json to the renamed embedded PDF
                QDir snbDir(extractedSnbPath);
                QString newRelativePath = snbDir.relativeFilePath(embeddedPdfPath);
                
                // Update the pdf_relative_path and pdf_path in document.json
                root["pdf_relative_path"] = newRelativePath;
                root["pdf_path"] = embeddedPdfPath;  // Set absolute path to embedded location
                
                // Write updated manifest
                if (manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QJsonDocument modifiedDoc(root);
                    manifestFile.write(modifiedDoc.toJson(QJsonDocument::Indented));
                    manifestFile.close();
                    
                    #ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "NotebookImporter: Updated pdf_relative_path to:" << newRelativePath;
                    qDebug() << "NotebookImporter: Updated pdf_path to:" << embeddedPdfPath;
                    #endif
                } else {
                    qWarning() << "NotebookImporter: Failed to update document.json with embedded PDF path";
                }
            }
        }
    }
    
    // Success!
    result.success = true;
    result.extractedSnbPath = extractedSnbPath;
    result.embeddedPdfPath = embeddedPdfPath;
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "NotebookImporter: Import successful!"
             << "Path:" << result.extractedSnbPath
             << "PDF:" << (result.embeddedPdfPath.isEmpty() ? "(none)" : result.embeddedPdfPath);
    #endif
    
    return result;
}

NotebookImporter::ImportResult NotebookImporter::importFolderPackage(
    const QString& packPath, const QString& parentDir, const QString& format)
{
    ImportResult result;
    const QString packName = QFileInfo(packPath).fileName();
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, packPath.toUtf8().constData(), 0)) {
        result.errorMessage = QObject::tr("Could not open \"%1\". The file is damaged or incomplete.")
                                  .arg(packName);
        return result;
    }
    QString tmp;
    auto fail = [&](const QString& why) {
        mz_zip_reader_end(&zip);
        if (!tmp.isEmpty()) QDir(tmp).removeRecursively();
        result.errorMessage = why;
        return result;
    };

    size_t size = 0;
    void* raw = mz_zip_reader_extract_file_to_heap(&zip, "speedynote-pack.json", &size, 0);
    const QJsonObject manifest = raw
        ? QJsonDocument::fromJson(QByteArray(static_cast<const char*>(raw), int(size))).object()
        : QJsonObject();
    mz_free(raw);
    const QString root = manifest.value("root").toString();
    if (manifest.value("format").toString() != format || root.isEmpty() || root == "."
        || root == ".." || root.contains('/') || root.contains('\\') || root.contains(':'))
        return fail(QObject::tr("\"%1\" is not a packed SpeedyNote notebook.").arg(packName));

    tmp = parentDir + "/." + root + ".unpacking";
    QDir(tmp).removeRecursively();  // left by an earlier unpack that was cut short
    if (!QDir().mkpath(tmp))
        return fail(QObject::tr("Could not create a folder in:\n%1").arg(parentDir));

    int files = 0;
    const int count = int(mz_zip_reader_get_num_files(&zip));
    for (int i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, mz_uint(i), &stat))
            return fail(QObject::tr("\"%1\" is damaged.").arg(packName));
        const QString name = QString::fromUtf8(stat.m_filename);
        if (name == "speedynote-pack.json") continue;
        const QString rel = name.mid(root.size() + 1);
        // Nothing may land outside the new folder.
        if (!name.startsWith(root + "/") || rel.contains('\\') || rel.contains(':')
            || rel.startsWith('/') || rel.split('/').contains(".."))
            return fail(QObject::tr("\"%1\" is damaged (%2).").arg(packName, name));
        const QString out = tmp + "/" + rel;
        if (stat.m_is_directory) {
            if (!QDir().mkpath(out)) return fail(QObject::tr("Could not create:\n%1").arg(out));
            continue;
        }
        size_t length = 0;
        void* data = QDir().mkpath(QFileInfo(out).absolutePath())
            ? mz_zip_reader_extract_to_heap(&zip, mz_uint(i), &length, 0)  // checks the CRC
            : nullptr;
        QFile file(out);
        const bool ok = data && file.open(QIODevice::WriteOnly)
            && file.write(static_cast<const char*>(data), qint64(length)) == qint64(length);
        mz_free(data);
        file.close();
        if (!ok) return fail(QObject::tr("\"%1\" is damaged: %2 could not be unpacked.").arg(packName, rel));
        ++files;
    }
    if (files != manifest.value("files").toInt(-1))
        return fail(QObject::tr("\"%1\" is incomplete: %2 of %3 files.")
                        .arg(packName).arg(files).arg(manifest.value("files").toInt()));
    mz_zip_reader_end(&zip);

    QString target = parentDir + "/" + root;
    for (int n = 2; QFileInfo::exists(target); ++n) target = QStringLiteral("%1/%2 (%3)").arg(parentDir, root).arg(n);
    if (!QDir().rename(tmp, target)) {
        QDir(tmp).removeRecursively();
        result.errorMessage = QObject::tr("Could not create:\n%1").arg(target);
        return result;
    }
    result.success = true;
    result.extractedSnbPath = target;
    return result;
}

QString NotebookImporter::resolveNameConflict(const QString& baseName, const QString& destDir)
{
    QDir dir(destDir);
    
    // If no conflict, return the original name
    if (!dir.exists(baseName)) {
        return baseName;
    }
    
    // Remove .snb extension for numbering
    QString nameWithoutExt = baseName;
    if (nameWithoutExt.endsWith(".snb", Qt::CaseInsensitive)) {
        nameWithoutExt = nameWithoutExt.left(nameWithoutExt.length() - 4);
    }
    
    // Find a unique name: "Notebook (1).snb", "Notebook (2).snb", etc.
    int counter = 1;
    QString newName;
    
    do {
        newName = QString("%1 (%2).snb").arg(nameWithoutExt).arg(counter);
        counter++;
        
        // Safety limit to avoid infinite loop
        if (counter > 1000) {
            qWarning() << "NotebookImporter: Too many duplicates for" << baseName;
            break;
        }
    } while (dir.exists(newName));
    
    return newName;
}

