#include "CornerRadiusSlider.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QTimer>

CornerRadiusSlider::CornerRadiusSlider(QWidget* parent)
    : QWidget(parent)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(10, 6, 10, 6);
    row->addWidget(new QLabel(tr("Sharp"), this));
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 100);
    m_slider->setPageStep(10);
    m_slider->setMinimumWidth(160);
    m_slider->setFocusPolicy(Qt::StrongFocus);
    m_slider->setToolTip(tr("How round the corners are"));
    row->addWidget(m_slider, 1);
    row->addWidget(new QLabel(tr("Round"), this));

    m_pause = new QTimer(this);
    m_pause->setSingleShot(true);
    m_pause->setInterval(300);
    connect(m_pause, &QTimer::timeout, this, &CornerRadiusSlider::finish);

    const auto open = [this]() {
        m_pause->stop();
        if (!m_open) {
            m_open = true;
            emit begin();
        }
    };
    connect(m_slider, &QSlider::sliderPressed, this, open);
    connect(m_slider, &QSlider::valueChanged, this, [this, open](int value) {
        if (m_quiet) {
            return;
        }
        open();
        emit preview(value / 100.0);
        if (!m_slider->isSliderDown()) {
            m_pause->start();   // A key, the wheel or a click on the groove
        }
    });
    connect(m_slider, &QSlider::sliderReleased, this, &CornerRadiusSlider::finish);
}

void CornerRadiusSlider::setFraction(qreal fraction)
{
    m_quiet = true;
    m_slider->setValue(qRound(qBound(0.0, fraction, 1.0) * 100.0));
    m_quiet = false;
}

void CornerRadiusSlider::finish()
{
    m_pause->stop();
    if (m_open) {
        m_open = false;
        emit commit();
    }
}
