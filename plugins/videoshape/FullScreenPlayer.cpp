/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2010 C. Boemann <cbo@boemann.dk>
 * SPDX-FileCopyrightText: 2012 Gopalakrishna Bhat A <gopalakbhat@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "FullScreenPlayer.h"

#include <KoIcon.h>

#include <KLocalizedString>

#include <QAudioOutput>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSlider>
#include <QVideoWidget>

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

FullScreenPlayer::FullScreenPlayer(const QUrl &url)
    : QWidget(nullptr)
    , m_seekSlider(new QSlider(Qt::Horizontal, this))
    , m_volumeSlider(new QSlider(Qt::Horizontal, this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    m_mediaObject = new QMediaPlayer(this);
    m_videoWidget = new QVideoWidget(this);
    m_audioOutput = new QAudioOutput(this);
    m_mediaObject->setVideoOutput(m_videoWidget);
    m_mediaObject->setAudioOutput(m_audioOutput);

    m_seekSlider->setRange(0, 1000);
    m_seekSlider->setEnabled(false);
    connect(m_mediaObject, &QMediaPlayer::seekableChanged, m_seekSlider, &QSlider::setEnabled);
    connect(m_seekSlider, &QSlider::valueChanged, this, [this](int value) {
        m_mediaObject->setPosition(m_mediaObject->duration() * value / m_seekSlider->maximum());
    });

    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(qRound(m_audioOutput->volume() * 100));
    m_volumeSlider->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_audioOutput->setVolume(value / 100.0f);
    });

    m_playbackTime = new QLabel(QString("00:00:00"), this);

    m_play = new QToolButton(this);
    m_play->setIcon(koIcon("media-playback-start"));
    m_play->setToolTip(i18n("Play"));
    connect(m_play, &QAbstractButton::clicked, this, &FullScreenPlayer::play);

    m_pause = new QToolButton(this);
    m_pause->setIcon(koIcon("media-playback-pause"));
    m_pause->setToolTip(i18n("Pause"));
    connect(m_pause, &QAbstractButton::clicked, this, &FullScreenPlayer::pause);

    m_stop = new QToolButton(this);
    m_stop->setIcon(koIcon("media-playback-stop"));
    m_stop->setToolTip(i18n("Stop"));
    connect(m_stop, &QAbstractButton::clicked, this, &FullScreenPlayer::stop);

    m_volumeIconMuted = new QToolButton(this);
    m_volumeIconMuted->setIcon(koIcon("audio-volume-muted"));
    m_volumeIconMuted->setToolTip(i18n("Unmute"));
    connect(m_volumeIconMuted, &QAbstractButton::clicked, this, &FullScreenPlayer::unmute);

    m_volumeIconUnmuted = new QToolButton(this);
    m_volumeIconUnmuted->setIcon(koIcon("audio-volume-medium"));
    m_volumeIconUnmuted->setToolTip(i18n("Mute"));
    connect(m_volumeIconUnmuted, &QAbstractButton::clicked, this, &FullScreenPlayer::mute);

    QHBoxLayout *playbackControls = new QHBoxLayout();
    playbackControls->addWidget(m_play);
    playbackControls->addWidget(m_pause);
    playbackControls->addWidget(m_stop);
    playbackControls->addWidget(m_seekSlider);
    playbackControls->addWidget(m_playbackTime);
    playbackControls->addWidget(m_volumeIconMuted);
    playbackControls->addWidget(m_volumeIconUnmuted);
    playbackControls->addWidget(m_volumeSlider);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(m_videoWidget);
    layout->addLayout(playbackControls);
    layout->setContentsMargins({});
    setLayout(layout);
    connect(m_audioOutput, &QAudioOutput::mutedChanged, this, &FullScreenPlayer::muteStateChanged);
    connect(m_mediaObject, &QMediaPlayer::playbackStateChanged, this, &FullScreenPlayer::playStateChanged);
    connect(m_mediaObject, &QMediaPlayer::positionChanged, this, &FullScreenPlayer::updatePlaybackTime);
    connect(m_mediaObject, &QMediaPlayer::durationChanged, this, [this] {
        updatePlaybackTime(m_mediaObject->position());
    });
    connect(m_mediaObject, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            stop();
    });
    connect(m_mediaObject, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        QMessageBox::warning(this, i18n("Video Playback Error"), message);
        stop();
    });

    muteStateChanged(m_audioOutput->isMuted());
    playStateChanged(m_mediaObject->playbackState());
    showFullScreen();
    m_mediaObject->setSource(url);
    play();
}

FullScreenPlayer::~FullScreenPlayer() = default;

void FullScreenPlayer::play()
{
    m_mediaObject->play();
}

void FullScreenPlayer::pause()
{
    m_mediaObject->pause();
}

void FullScreenPlayer::stop()
{
    m_mediaObject->stop();
    close();
}

void FullScreenPlayer::mute()
{
    m_audioOutput->setMuted(true);
}

void FullScreenPlayer::unmute()
{
    m_audioOutput->setMuted(false);
}

void FullScreenPlayer::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    m_mediaObject->stop();
    close();
}

void FullScreenPlayer::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        m_mediaObject->stop();
        close();
    }
}

void FullScreenPlayer::playStateChanged(QMediaPlayer::PlaybackState state)
{
    const bool playing = state == QMediaPlayer::PlayingState;
    m_play->setVisible(!playing);
    m_pause->setVisible(playing);
}

void FullScreenPlayer::updatePlaybackTime(qint64 currentTime)
{
    QString currentPlayTime = QString("%1:%2:%3")
                                  .arg((currentTime / 3600000) % 60, 2, 10, QChar('0'))
                                  .arg((currentTime / 60000) % 60, 2, 10, QChar('0'))
                                  .arg((currentTime / 1000) % 60, 2, 10, QChar('0'));

    const qint64 time = m_mediaObject->duration();
    const QSignalBlocker blocker(m_seekSlider);
    m_seekSlider->setValue(time > 0 ? currentTime * m_seekSlider->maximum() / time : 0);
    QString totalTime = QString("%1:%2:%3")
                            .arg((time / 3600000) % 60, 2, 10, QChar('0'))
                            .arg((time / 60000) % 60, 2, 10, QChar('0'))
                            .arg((time / 1000) % 60, 2, 10, QChar('0'));

    m_playbackTime->setText(QString("%1/%2").arg(currentPlayTime, totalTime));
}

void FullScreenPlayer::muteStateChanged(bool muted)
{
    if (muted) {
        m_volumeIconMuted->setVisible(true);
        m_volumeIconUnmuted->setVisible(false);
    } else {
        m_volumeIconMuted->setVisible(false);
        m_volumeIconUnmuted->setVisible(true);
    }
}
