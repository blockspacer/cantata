/*
 * Cantata
 *
 * Copyright (c) 2011-2017 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "nowplayingwidget.h"
#include "ratingwidget.h"
#include "volumeslider.h"
#include "mpd-interface/song.h"
#include "gui/settings.h"
#include "mpd-interface/mpdconnection.h"
#include "models/playqueuemodel.h"
#include "support/utils.h"
#ifdef Q_OS_MAC
#include "support/osxstyle.h"
#endif
#include <QLabel>
#include <QBoxLayout>
#include <QProxyStyle>
#include <QTimer>
#include <QApplication>
#include <QMouseEvent>
#include <QSlider>
#include <QToolTip>
#include <QSpacerItem>
#include <QToolButton>

static const int constPollMpd = 2; // Poll every 2 seconds when playing

class PosSliderProxyStyle : public QProxyStyle
{
public:
    PosSliderProxyStyle()
        : QProxyStyle()
    {
        setBaseStyle(qApp->style());
    }

    int styleHint(StyleHint stylehint, const QStyleOption *opt, const QWidget *widget, QStyleHintReturn *returnData) const
    {
        if (QStyle::SH_Slider_AbsoluteSetButtons==stylehint) {
            return Qt::LeftButton|QProxyStyle::styleHint(stylehint, opt, widget, returnData);
        } else {
            return QProxyStyle::styleHint(stylehint, opt, widget, returnData);
        }
    }
};

class TimeLabel : public QLabel
{
public:
    TimeLabel(QWidget *p, QSlider *s)
        : QLabel(p)
        , slider(s)
        , pressed(false)
        , showRemaining(Settings::self()->showTimeRemaining())
    {
        setAttribute(Qt::WA_Hover, true);
        setAlignment((isRightToLeft() ? Qt::AlignLeft : Qt::AlignRight)|Qt::AlignVCenter);
        // For some reason setting this here does not work!
        // setStyleSheet(QLatin1String("QLabel:hover {color:palette(highlight);}"));
    }

    void setRange(int min, int max)
    {
        QLabel::setEnabled(min!=max);
        if (!isEnabled()) {
            setText(QLatin1String(" "));
        }
    }

    void updateTime()
    {
        if (isEnabled()) {
            int value=showRemaining ? slider->maximum()-slider->value() : slider->maximum();
            QString prefix=showRemaining && value ? QLatin1String("-") : QString();
            if (isRightToLeft()) {
                setText(QString("%1 / %2").arg(prefix+Utils::formatTime(value), Utils::formatTime(slider->value())));
            } else {
                setText(QString("%1 / %2").arg(Utils::formatTime(slider->value()), prefix+Utils::formatTime(value)));
            }
        } else {
            setText(QLatin1String(" "));
        }
    }

    void saveConfig()
    {
        Settings::self()->saveShowTimeRemaining(showRemaining);
    }

    bool event(QEvent *e)
    {
        switch (e->type()) {
        case QEvent::MouseButtonPress:
            if (isEnabled() && Qt::NoModifier==static_cast<QMouseEvent *>(e)->modifiers() && Qt::LeftButton==static_cast<QMouseEvent *>(e)->button()) {
                pressed=true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (isEnabled() && pressed) {
                showRemaining=!showRemaining;
                updateTime();
            }
            pressed=false;
            break;
        case QEvent::HoverEnter:
            if (isEnabled()) {
                #ifdef Q_OS_MAC
                setStyleSheet(QString("QLabel{color:%1;}").arg(OSXStyle::self()->viewPalette().highlight().color().name()));
                #else
                setStyleSheet(QLatin1String("QLabel{color:palette(highlight);}"));
                #endif
            }
            break;
        case QEvent::HoverLeave:
            if (isEnabled()) {
                setStyleSheet(QString("QLabel{color:%1;}").arg(((NowPlayingWidget *)parentWidget())->textColor().name()));
            }
        default:
            break;
        }
        return QLabel::event(e);
    }

protected:
    QSlider *slider;
    bool pressed;
    bool showRemaining;
};

PosSlider::PosSlider(QWidget *p)
    : QSlider(p)
{
    setPageStep(0);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::NoFocus);
    setStyle(new PosSliderProxyStyle());
    int h=qMax((int)(fontMetrics().height()*0.5), 8);
    setMinimumHeight(h);
    setMaximumHeight(h);
    setMouseTracking(true);
}

void PosSlider::updateStyleSheet(const QColor &col)
{
    int lineWidth=maximumHeight()>12 ? 2 : 1;

    QString boderFormat=QLatin1String("QSlider::groove:horizontal { border: %1px solid rgba(%2, %3, %4, %5); "
                                      "background: solid rgba(%2, %3, %4, %6); "
                                      "border-radius: %7px } ");
    QString fillFormat=QLatin1String("QSlider::")+QLatin1String(isRightToLeft() ? "add" : "sub")+
                       QLatin1String("-page:horizontal {border: %1px solid rgb(%3, %4, %5); "
                                     "background: solid rgb(%3, %4, %5); "
                                     "border-radius: %1px; margin: %2px} ")+
                       QLatin1String("QSlider::")+QLatin1String(isRightToLeft() ? "add" : "sub")+
                       QLatin1String("-page:horizontal:disabled {border: 0px; background: solid rgba(0, 0, 0, 0)}");

    #ifdef Q_OS_MAC
    QColor fillColor=OSXStyle::self()->viewPalette().highlight().color();
    #else
    QColor fillColor=qApp->palette().highlight().color();
    #endif
    int alpha=col.value()<32 ? 96 : 64;

    setStyleSheet(boderFormat.arg(lineWidth).arg(col.red()).arg(col.green()).arg(col.blue()).arg(alpha)
                             .arg(alpha/4).arg(lineWidth*2)+
                  fillFormat.arg(lineWidth).arg(lineWidth*2).arg(fillColor.red()).arg(fillColor.green()).arg(fillColor.blue()));
}

void PosSlider::mouseMoveEvent(QMouseEvent *e)
{
    if (maximum()!=minimum()) {
        qreal pc = (qreal)e->pos().x()/(qreal)width();
        QPoint pos(e->pos().x(), height());
        QToolTip::showText(mapToGlobal(pos), Utils::formatTime(maximum()*pc), this, rect());
    }

    QSlider::mouseMoveEvent(e);
}

void PosSlider::wheelEvent(QWheelEvent *ev)
{
    if (!isEnabled()) {
        return;
    }

    static const int constStep=5;
    int numDegrees = ev->delta() / 8;
    int numSteps = numDegrees / 15;
    int val=value();
    if (numSteps > 0) {
        int max=maximum();
        if (val!=max) {
            for (int i = 0; i < numSteps; ++i) {
                val+=constStep;
                if (val>max) {
                    val=max;
                    break;
                }
            }
        }
    } else {
        int min=minimum();
        if (val!=min) {
            for (int i = 0; i > numSteps; --i) {
                val-=constStep;
                if (val<min) {
                    val=min;
                    break;
                }
            }
        }
    }
    if (val!=value()) {
        setValue(val);
        emit positionSet();
    }
}

void PosSlider::setRange(int min, int max)
{
    bool active=min!=max;
    QSlider::setRange(min, max);
    setValue(min);
    if (!active) {
        setToolTip(QString());
    }

    setEnabled(active);
}

NowPlayingWidget::NowPlayingWidget(QWidget *p)
    : QWidget(p)
    , timer(0)
    , lastVal(0)
    , pollCount(0)
{
    track=new SqueezedTextLabel(this);
    track->setTextInteractionFlags(Qt::TextSelectableByMouse);
    artist=new SqueezedTextLabel(this);
    artist->setTextInteractionFlags(Qt::TextSelectableByMouse);
    slider=new PosSlider(this);
    time=new TimeLabel(this, slider);
    ratingWidget=new RatingWidget(this);
    infoLabel=new QLabel(this);
    QFont f=track->font();
    QFont small=Utils::smallFont(f);
    f.setBold(true);
    track->setFont(f);
    artist->setFont(small);
    time->setFont(small);
    infoLabel->setFont(small);
    slider->setOrientation(Qt::Horizontal);
    QBoxLayout *layout=new QBoxLayout(QBoxLayout::TopToBottom, this);
    QBoxLayout *topLayout=new QBoxLayout(QBoxLayout::LeftToRight, 0);
    QBoxLayout *botLayout=new QBoxLayout(QBoxLayout::LeftToRight, 0);
    int space=Utils::layoutSpacing(this);
    int pad=qMax(space, Utils::scaleForDpi(8));
    #ifdef Q_OS_MAC
    layout->setContentsMargins(pad, 0, pad, 0);
    #else
    layout->setContentsMargins(pad, space, pad, space);
    #endif
    layout->setSpacing(space/2);
    topLayout->setMargin(0);
    botLayout->setMargin(0);
    topLayout->setSpacing(space/2);
    botLayout->setSpacing(space/2);
    topLayout->addWidget(track);
    topLayout->addWidget(ratingWidget);
    topLayout->addWidget(infoLabel);
    layout->addLayout(topLayout);
    botLayout->addWidget(artist);
    botLayout->addWidget(time);
    layout->addLayout(botLayout);
    layout->addItem(new QSpacerItem(1, space/4, QSizePolicy::Fixed, QSizePolicy::Fixed));
    layout->addWidget(slider);
    connect(slider, SIGNAL(sliderPressed()), this, SLOT(pressed()));
    connect(slider, SIGNAL(sliderReleased()), this, SLOT(released()));
    connect(slider, SIGNAL(positionSet()), this, SIGNAL(sliderReleased()));
    connect(slider, SIGNAL(valueChanged(int)), this, SLOT(updateTimes()));
    connect(this, SIGNAL(mpdPoll()), MPDConnection::self(), SLOT(getStatus()));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    clearTimes();
    update(Song());
    connect(ratingWidget, SIGNAL(valueChanged(int)), SLOT(setRating(int)));
    connect(this, SIGNAL(setRating(QString,quint8)), MPDConnection::self(), SLOT(setRating(QString,quint8)));
    connect(PlayQueueModel::self(), SIGNAL(currentSongRating(QString,quint8)), this, SLOT(rating(QString,quint8)));
    connect(MPDStatus::self(), SIGNAL(updated()), this, SLOT(updateInfo()));
}

void NowPlayingWidget::update(const Song &song)
{
    QString name=song.name();
    currentSongFile=song.file;
    ratingWidget->setEnabled(!song.isEmpty() && Song::Standard==song.type);
    ratingWidget->setValue(0);
    updateInfo();
    if (song.isEmpty()) {
        track->setText(" ");
        artist->setText(" ");
    } else if (song.isStream() && !song.isCantataStream() && !song.isCdda() && !song.isDlnaStream()) {
        track->setText(name.isEmpty() ? Song::unknown() : name);
        if (song.artist.isEmpty() && song.title.isEmpty() && !name.isEmpty()) {
            artist->setText(tr("(Stream)"));
        } else {
            artist->setText(song.artist.isEmpty() ? song.title : song.artistSong());
        }
    } else {
        if (song.title.isEmpty() && song.artist.isEmpty() && (!name.isEmpty() || !song.file.isEmpty())) {
            track->setText(name.isEmpty() ? song.file : name);
        } else {
            track->setText(song.title+(song.origYear>0 && song.origYear!=song.year ? QLatin1String(" (")+QString::number(song.origYear)+QLatin1Char(')') : QString()));
        }
        if (song.album.isEmpty() && song.artist.isEmpty()) {
            artist->setText(track->fullText().isEmpty() ? QString() : Song::unknown());
        } else if (song.album.isEmpty()) {
            artist->setText(song.artist);
        } else {
            // Artist here is always artist, and not album artist or composer
            artist->setText(song.artist+QString(" – ")+song.displayAlbum(false));
        }
    }
}

void NowPlayingWidget::startTimer()
{
    if (!timer) {
        timer=new QTimer(this);
        timer->setInterval(1000);
        connect(timer, SIGNAL(timeout()), this, SLOT(updatePos()));
    }
    startTime.restart();
    lastVal=value();
    timer->start();
    pollCount=0;
}

void NowPlayingWidget::stopTimer()
{
    if (timer) {
        timer->stop();
    }
    pollCount=0;
}

void NowPlayingWidget::setValue(int v)
{
    startTime.restart();
    lastVal=v;
    slider->setValue(v);
    updateTimes();
}

void NowPlayingWidget::setRange(int min, int max)
{
    slider->setRange(min, max);
    time->setRange(min, max);
    updateTimes();
}

void NowPlayingWidget::clearTimes()
{
    stopTimer();
    lastVal=0;
    slider->setRange(0, 0);
    time->setRange(0, 0);
    time->updateTime();
}

int NowPlayingWidget::value() const
{
    return slider->value();
}

void NowPlayingWidget::readConfig()
{
    ratingWidget->setVisible(Settings::self()->showRatingWidget());
    infoLabel->setVisible(Settings::self()->showTechnicalInfo());
}

void NowPlayingWidget::saveConfig()
{
    time->saveConfig();
}

void NowPlayingWidget::rating(const QString &file, quint8 r)
{
    if (file==currentSongFile) {
        ratingWidget->setValue(r);
    }
}

void NowPlayingWidget::updateTimes()
{
    if (slider->value()<172800 && slider->value() != slider->maximum()) {
        time->updateTime();
    }
}

void NowPlayingWidget::updatePos()
{
    quint16 elapsed=(startTime.elapsed()/1000.0)+0.5;
    slider->setValue(lastVal+elapsed);
    MPDStatus::self()->setGuessedElapsed(lastVal+elapsed);
    if (++pollCount>=constPollMpd) {
        pollCount=0;
        emit mpdPoll();
    }
}

void NowPlayingWidget::pressed()
{
    if (timer) {
        timer->stop();
    }
}

void NowPlayingWidget::released()
{
    if (timer) {
        timer->start();
    }
    emit sliderReleased();
}

void NowPlayingWidget::setRating(int v)
{
    emit setRating(currentSongFile, v);
}

void NowPlayingWidget::updateInfo()
{
    if (currentSongFile.isEmpty()) {
        infoLabel->setText(QString());
        return;
    }

    QString info;
    info = MPDStatus::self()->bitrate()>0
            ? MPDStatus::self()->samplerate()>0
                ? info.sprintf("%d kbps, %.1f kHz", MPDStatus::self()->bitrate(), MPDStatus::self()->samplerate()/1000.0)
                : info.sprintf("%d kbps", MPDStatus::self()->bitrate())
            : MPDStatus::self()->samplerate()>0
                  ? info.sprintf("%.1f kHz", MPDStatus::self()->samplerate()/1000.0)
                  : QString();

    int pos=currentSongFile.lastIndexOf('.');
    if (pos>1) {
        QString ext = currentSongFile.mid(pos+1).toUpper();

        if (ext.length()<5) {
            if (!info.isEmpty()) {
                info+=", ";
            }
            info+=ext;
        }
    }
    infoLabel->setText(info);
}

void NowPlayingWidget::initColors()
{
    ensurePolished();
    QToolButton btn(this);
    btn.ensurePolished();
    track->setPalette(btn.palette());
    artist->setPalette(btn.palette());
    time->setPalette(btn.palette());
    slider->updateStyleSheet(track->palette().windowText().color());
    ratingWidget->setColor(Utils::clampColor(track->palette().buttonText().color()));
    infoLabel->setPalette(btn.palette());
}
