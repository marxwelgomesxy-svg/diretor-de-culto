#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <thread>
#include <QWidget>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <vector>

struct obs_source;
typedef struct obs_source obs_source_t;
struct video_data;
struct audio_data;

class QLabel;
class QPushButton;
class QProgressBar;
class QRadioButton;
class QButtonGroup;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QTableWidget;
class QPushButton;

class DiretorDock final : public QWidget {
    Q_OBJECT

    // These types must be declared before the Qt slots that use them.
    // Keeping them inside the class also lets Qt MOC see the complete signatures.
    enum class Mode { Manual, Assistido, Automatico };
    enum class Engine { IA, Predefinido };
    enum class RequestKind { None, Test, Analyze };

public:
    explicit DiretorDock(QWidget *parent = nullptr);
    ~DiretorDock() override;

public slots:
    void refreshFromObs();

private slots:
    void analyze();
    void captureCandidates();
    void cutSuggestion();
    void ignoreSuggestion();
    void modeChanged();
    void engineChanged();
    void addPredefinedRule();
    void removePredefinedRule();
    void loadDefaultRules();
    void predefinedRuleCellChanged();
    void analyzeNow();
    void testAIConnection();
    void toggleDirector();
    void updateProgress();
    void handleAIResult(RequestKind kind, int httpStatus, const QString &errorText, const QByteArray &raw);
    void startGeminiHttpRequest(RequestKind kind, const QString &url, const QByteArray &body);

private:
    struct Suggestion {
        QString scene;
        int confidence = 0;
        QStringList reasons;
        QString source = QStringLiteral("IA Gemini");
        int evidenceCount = 0;
    };

    QString currentSceneName() const;
    QStringList allSceneNames() const;
    QStringList candidateSceneNames() const;
    void applyScene(const QString &sceneName);
    void updateUi();
    void setStatus(const QString &text, bool active = true);
    void setAIStatus(const QString &text, bool online);
    void startLoop();
    void stopLoop();
    void startMediaAnalysis();
    void stopMediaAnalysis();
    void updateStreamingState();
    void buildCandidateGrid();
    void updateCandidateGrid();
    bool renderSourceToImage(obs_source_t *source, QImage &out) const;
    void captureSceneImages();
    void requestGemini();
    void requestGeminiTest();
    QJsonObject buildGeminiBody(bool includeMedia) const;
    QString buildAIPrompt() const;
    QByteArray buildWavAudio() const;
    QString apiKey() const;
    QString modelName() const;
    void saveApiSettings();
    void processGeminiDecision(const QJsonObject &decision);
    void evaluatePredefinedRules();
    void loadPredefinedRules();
    void savePredefinedRules();
    QString ruleKey(const QString &trigger, const QString &target, int delay) const;

    static void rawVideoCallback(void *param, struct video_data *frame);
    static void rawAudioCallback(void *param, size_t mix_idx, struct audio_data *data);
    void processVideoFrame(struct video_data *frame);
    void processAudio(struct audio_data *data);

    QLabel *statusLabel_ = nullptr;
    QLabel *liveSceneLabel_ = nullptr;
    QLabel *liveBadge_ = nullptr;
    QLabel *livePreview_ = nullptr;
    QLabel *suggestionSceneLabel_ = nullptr;
    QLabel *suggestionPreview_ = nullptr;
    QLabel *confidenceLabel_ = nullptr;
    QLabel *reasonsLabel_ = nullptr;
    QLabel *aiStatusLabel_ = nullptr;
    QLabel *audioStatusLabel_ = nullptr;
    QLabel *motionStatusLabel_ = nullptr;
    QLabel *nextAnalysisLabel_ = nullptr;
    QLabel *timeLabel_ = nullptr;
    QLabel *cutsLabel_ = nullptr;
    QLabel *mostUsedLabel_ = nullptr;
    QLabel *apiStateLabel_ = nullptr;

    QProgressBar *progressBar_ = nullptr;
    QPushButton *cutButton_ = nullptr;
    QPushButton *ignoreButton_ = nullptr;
    QPushButton *analyzeButton_ = nullptr;
    QPushButton *testAIButton_ = nullptr;
    QPushButton *directorToggleButton_ = nullptr;
    QRadioButton *manualRadio_ = nullptr;
    QRadioButton *assistidoRadio_ = nullptr;
    QRadioButton *automaticoRadio_ = nullptr;
    QButtonGroup *modeGroup_ = nullptr;
    QRadioButton *iaRadio_ = nullptr;
    QRadioButton *predefRadio_ = nullptr;
    QButtonGroup *engineGroup_ = nullptr;
    QTableWidget *rulesTable_ = nullptr;
    QPushButton *addRuleButton_ = nullptr;
    QPushButton *removeRuleButton_ = nullptr;
    QPushButton *defaultRulesButton_ = nullptr;
    QLineEdit *modelEdit_ = nullptr;
    QLineEdit *apiKeyEdit_ = nullptr;

    QTimer *analysisTimer_ = nullptr;
    QTimer *progressTimer_ = nullptr;
    QTimer *captureTimer_ = nullptr;
    std::thread networkThread_;
    std::atomic<bool> networkBusy_{false};
    RequestKind requestKind_ = RequestKind::None;

    Mode mode_ = Mode::Assistido;
    Engine engine_ = Engine::IA;
    bool directorEnabled_ = false;
    bool streamingActive_ = false;
    bool previousStreamingState_ = false;
    bool aiBusy_ = false;
    bool aiOnline_ = false;

    Suggestion suggestion_;
    QString lastIgnoredScene_;
    QString mostUsedScene_;
    int mostUsedCount_ = 0;
    int cuts_ = 0;
    qint64 elapsedSeconds_ = 0;
    qint64 secondsSinceCut_ = 9999;
    qint64 secondsSinceAnalysis_ = 0;
    qint64 secondsSinceAI_ = 9999;
    qint64 secondsInCurrentScene_ = 0;
    qint64 aiCooldownSeconds_ = 0;
    QString observedScene_;
    QString lastCutScene_;
    QVector<QString> recentScenes_;
    QSet<QString> triggeredRuleKeys_;

    std::atomic<float> audioRms_{0.0f};
    std::atomic<float> motionScore_{0.0f};
    std::atomic<uint64_t> videoFrameCounter_{0};
    std::atomic<bool> mediaActive_{false};

    mutable QMutex frameMutex_;
    mutable QMutex audioMutex_;
    mutable QMutex captureMutex_;
    QImage latestFrame_;
    std::vector<unsigned char> previousLuma_;
    std::vector<float> audioBuffer_;
    QHash<QString, QImage> candidateFrames_;
    QVector<QLabel *> candidateThumbs_;
    QVector<QString> candidateNames_;
    uint64_t lastCopiedFrame_ = 0;
};
