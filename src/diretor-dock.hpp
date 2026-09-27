#pragma once

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QWidget>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <atomic>
#include <chrono>
#include <vector>

class QLabel;
class QPushButton;
class QProgressBar;
class QRadioButton;
class QButtonGroup;
class QGroupBox;
class QLineEdit;
class QNetworkReply;
class QCheckBox;

class DiretorDock final : public QWidget {
    Q_OBJECT

public:
    explicit DiretorDock(QWidget *parent = nullptr);
    ~DiretorDock() override;

public slots:
    void refreshFromObs();

private slots:
    void analyze();
    void cutSuggestion();
    void ignoreSuggestion();
    void modeChanged();
    void askLocalAI();
    void testAIConnection();
    void installAI();
    void toggleDirector();
    void updateProgress();
    void onAIReply();

private:
    enum class Mode { Manual, Assistido, Automatico };

    struct Suggestion {
        QString scene;
        int confidence = 0;
        QStringList reasons;
        QString source = QStringLiteral("Motor local");
    };

    QString currentSceneName() const;
    QStringList allSceneNames() const;
    Suggestion buildRuleSuggestion(const QString &current) const;
    void applyScene(const QString &sceneName);
    void updateUi();
    void setStatus(const QString &text, bool active = true);
    QString displaySceneName(const QString &scene) const;
    QString buildAIPrompt(const QString &current) const;
    void requestAI(const QImage &frame);
    void setAIStatus(const QString &text, bool online);
    void startMediaAnalysis();
    void stopMediaAnalysis();
    void startLoop();
    void stopLoop();
    void updateStreamingState();

    static void rawVideoCallback(void *param, struct video_data *frame);
    static void rawAudioCallback(void *param, size_t mix_idx, struct audio_data *data);

    void processVideoFrame(struct video_data *frame);
    void processAudio(struct audio_data *data);

    QLabel *statusLabel_ = nullptr;
    QLabel *liveSceneLabel_ = nullptr;
    QLabel *liveBadge_ = nullptr;
    QLabel *suggestionSceneLabel_ = nullptr;
    QLabel *confidenceLabel_ = nullptr;
    QLabel *reasonsLabel_ = nullptr;
    QLabel *aiStatusLabel_ = nullptr;
    QLabel *audioStatusLabel_ = nullptr;
    QLabel *motionStatusLabel_ = nullptr;
    QLabel *analysisContextLabel_ = nullptr;
    QLabel *nextAnalysisLabel_ = nullptr;
    QLabel *timeLabel_ = nullptr;
    QLabel *cutsLabel_ = nullptr;
    QLabel *mostUsedLabel_ = nullptr;

    QProgressBar *progressBar_ = nullptr;
    QPushButton *cutButton_ = nullptr;
    QPushButton *ignoreButton_ = nullptr;
    QPushButton *aiButton_ = nullptr;
    QPushButton *testAIButton_ = nullptr;
    QPushButton *installAIButton_ = nullptr;
    QPushButton *directorToggleButton_ = nullptr;
    QRadioButton *manualRadio_ = nullptr;
    QRadioButton *assistidoRadio_ = nullptr;
    QRadioButton *automaticoRadio_ = nullptr;
    QButtonGroup *modeGroup_ = nullptr;
    QLineEdit *modelEdit_ = nullptr;

    QTimer *analysisTimer_ = nullptr;
    QTimer *progressTimer_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    QNetworkReply *pendingReply_ = nullptr;

    Mode mode_ = Mode::Assistido;
    bool directorEnabled_ = false;
    bool streamingActive_ = false;
    bool previousStreamingState_ = false;
    Suggestion suggestion_;
    QString lastIgnoredScene_;

    int cuts_ = 0;
    qint64 elapsedSeconds_ = 0;
    qint64 secondsSinceCut_ = 9999;
    qint64 secondsSinceAnalysis_ = 0;
    qint64 secondsSinceAI_ = 9999;
    QString mostUsedScene_;
    int mostUsedCount_ = 0;
    std::chrono::steady_clock::time_point lastCutTime_;

    std::atomic<float> audioRms_{0.0f};
    std::atomic<float> motionScore_{0.0f};
    std::atomic<uint64_t> videoFrameCounter_{0};
    std::atomic<bool> mediaActive_{false};

    QMutex frameMutex_;
    QImage latestFrame_;
    std::vector<unsigned char> previousLuma_;
    uint64_t lastCopiedFrame_ = 0;
    bool aiBusy_ = false;
    bool aiOnline_ = false;
    QString aiLastDecision_;
};
