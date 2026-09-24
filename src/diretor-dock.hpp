#pragma once

#include <QWidget>
#include <QTimer>
#include <QString>
#include <QStringList>

#include <chrono>

class QLabel;
class QPushButton;
class QProgressBar;
class QRadioButton;
class QButtonGroup;
class QGroupBox;

class DiretorDock final : public QWidget {
    Q_OBJECT

public:
    explicit DiretorDock(QWidget *parent = nullptr);
    ~DiretorDock() override = default;

public slots:
    void refreshFromObs();

private slots:
    void analyze();
    void cutSuggestion();
    void ignoreSuggestion();
    void modeChanged();

private:
    enum class Mode {
        Manual,
        Assistido,
        Automatico
    };

    struct Suggestion {
        QString scene;
        int confidence = 0;
        QStringList reasons;
    };

    QString currentSceneName() const;
    Suggestion buildSuggestion(const QString &current) const;
    void applyScene(const QString &sceneName);
    void updateUi();
    void setStatus(const QString &text, bool active = true);
    void updateProgress();
    QString displaySceneName(const QString &scene) const;

    QLabel *statusLabel_ = nullptr;
    QLabel *liveSceneLabel_ = nullptr;
    QLabel *liveBadge_ = nullptr;

    QLabel *suggestionSceneLabel_ = nullptr;
    QLabel *confidenceLabel_ = nullptr;
    QLabel *reasonsLabel_ = nullptr;

    QLabel *nextAnalysisLabel_ = nullptr;
    QProgressBar *progressBar_ = nullptr;

    QLabel *timeLabel_ = nullptr;
    QLabel *cutsLabel_ = nullptr;
    QLabel *mostUsedLabel_ = nullptr;

    QPushButton *cutButton_ = nullptr;
    QPushButton *ignoreButton_ = nullptr;

    QRadioButton *manualRadio_ = nullptr;
    QRadioButton *assistidoRadio_ = nullptr;
    QRadioButton *automaticoRadio_ = nullptr;
    QButtonGroup *modeGroup_ = nullptr;

    QTimer *analysisTimer_ = nullptr;
    QTimer *progressTimer_ = nullptr;

    Mode mode_ = Mode::Assistido;
    Suggestion suggestion_;
    QString lastIgnoredScene_;

    int cuts_ = 0;
    qint64 elapsedSeconds_ = 0;
    qint64 secondsSinceCut_ = 9999;
    qint64 secondsSinceAnalysis_ = 0;

    QString mostUsedScene_;
    int mostUsedCount_ = 0;
    std::chrono::steady_clock::time_point lastCutTime_;
};
