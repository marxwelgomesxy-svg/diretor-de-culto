#include "diretor-dock.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QButtonGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace {

constexpr int kAnalysisIntervalMs = 1000;
constexpr int kSuggestionEverySeconds = 8;
constexpr int kMinimumCutIntervalSeconds = 12;

QString normalize(const QString &value)
{
    return value.trimmed().toUpper();
}

QString joinReasons(const QStringList &reasons)
{
    QString result;
    for (const QString &reason : reasons)
        result += QStringLiteral("✓ ") + reason + QStringLiteral("\n");
    return result.trimmed();
}

QLabel *makeSectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

} // namespace

DiretorDock::DiretorDock(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DiretorDeCulto"));
    setMinimumWidth(300);
    setMinimumHeight(620);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto *titleRow = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("DIRETOR DE CULTO"));
    title->setObjectName(QStringLiteral("mainTitle"));

    statusLabel_ = new QLabel(QStringLiteral("● SISTEMA ATIVO"));
    statusLabel_->setObjectName(QStringLiteral("activeStatus"));

    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(statusLabel_);
    root->addLayout(titleRow);

    auto *liveBox = new QGroupBox(QStringLiteral("CÂMERA / CENA NO AR"));
    auto *liveLayout = new QVBoxLayout(liveBox);

    liveSceneLabel_ = new QLabel(QStringLiteral("Aguardando OBS..."));
    liveSceneLabel_->setObjectName(QStringLiteral("liveScene"));

    liveBadge_ = new QLabel(QStringLiteral("● NO AR"));
    liveBadge_->setObjectName(QStringLiteral("liveBadge"));

    liveLayout->addWidget(liveSceneLabel_);
    liveLayout->addWidget(liveBadge_);
    root->addWidget(liveBox);

    auto *suggestionBox = new QGroupBox(QStringLiteral("SUGESTÃO DE CORTE"));
    auto *suggestionLayout = new QVBoxLayout(suggestionBox);

    suggestionSceneLabel_ = new QLabel(QStringLiteral("Analisando..."));
    suggestionSceneLabel_->setObjectName(QStringLiteral("suggestionScene"));

    confidenceLabel_ = new QLabel(QStringLiteral("Confiança: --"));
    confidenceLabel_->setObjectName(QStringLiteral("confidence"));

    reasonsLabel_ = new QLabel(QStringLiteral("Aguardando análise do motor de regras."));
    reasonsLabel_->setWordWrap(true);
    reasonsLabel_->setObjectName(QStringLiteral("reasons"));

    suggestionLayout->addWidget(suggestionSceneLabel_);
    suggestionLayout->addWidget(confidenceLabel_);
    suggestionLayout->addWidget(reasonsLabel_);

    auto *buttons = new QHBoxLayout();
    cutButton_ = new QPushButton(QStringLiteral("CORTAR"));
    ignoreButton_ = new QPushButton(QStringLiteral("IGNORAR"));

    cutButton_->setObjectName(QStringLiteral("cutButton"));
    ignoreButton_->setObjectName(QStringLiteral("ignoreButton"));

    buttons->addWidget(cutButton_);
    buttons->addWidget(ignoreButton_);
    suggestionLayout->addLayout(buttons);

    root->addWidget(suggestionBox);

    auto *analysisBox = new QGroupBox(QStringLiteral("PRÓXIMA ANÁLISE"));
    auto *analysisLayout = new QVBoxLayout(analysisBox);

    progressBar_ = new QProgressBar();
    progressBar_->setRange(0, kSuggestionEverySeconds * 10);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(false);

    nextAnalysisLabel_ = new QLabel(QStringLiteral("Analisando em 8s"));
    analysisLayout->addWidget(progressBar_);
    analysisLayout->addWidget(nextAnalysisLabel_);

    root->addWidget(analysisBox);

    auto *modeBox = new QGroupBox(QStringLiteral("MODO DE OPERAÇÃO"));
    auto *modeLayout = new QVBoxLayout(modeBox);

    manualRadio_ = new QRadioButton(QStringLiteral("Manual"));
    assistidoRadio_ = new QRadioButton(QStringLiteral("Assistido"));
    automaticoRadio_ = new QRadioButton(QStringLiteral("Automático"));

    assistidoRadio_->setChecked(true);

    modeGroup_ = new QButtonGroup(this);
    modeGroup_->addButton(manualRadio_, static_cast<int>(Mode::Manual));
    modeGroup_->addButton(assistidoRadio_, static_cast<int>(Mode::Assistido));
    modeGroup_->addButton(automaticoRadio_, static_cast<int>(Mode::Automatico));

    modeLayout->addWidget(manualRadio_);
    modeLayout->addWidget(assistidoRadio_);
    modeLayout->addWidget(automaticoRadio_);
    root->addWidget(modeBox);

    auto *summaryBox = new QGroupBox(QStringLiteral("RESUMO DO CULTO"));
    auto *summaryLayout = new QVBoxLayout(summaryBox);

    timeLabel_ = new QLabel(QStringLiteral("Transmissão: 00:00:00"));
    cutsLabel_ = new QLabel(QStringLiteral("Cortes: 0"));
    mostUsedLabel_ = new QLabel(QStringLiteral("Mais usada: --"));

    summaryLayout->addWidget(timeLabel_);
    summaryLayout->addWidget(cutsLabel_);
    summaryLayout->addWidget(mostUsedLabel_);
    root->addWidget(summaryBox);

    root->addStretch();

    connect(cutButton_, &QPushButton::clicked, this, &DiretorDock::cutSuggestion);
    connect(ignoreButton_, &QPushButton::clicked, this, &DiretorDock::ignoreSuggestion);
    connect(modeGroup_, &QButtonGroup::idClicked, this, [this](int) {
        modeChanged();
    });

    analysisTimer_ = new QTimer(this);
    analysisTimer_->setInterval(kAnalysisIntervalMs);
    connect(analysisTimer_, &QTimer::timeout, this, &DiretorDock::analyze);
    analysisTimer_->start();

    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(100);
    connect(progressTimer_, &QTimer::timeout, this, &DiretorDock::updateProgress);
    progressTimer_->start();

    lastCutTime_ = std::chrono::steady_clock::now();

    setStyleSheet(QStringLiteral(R"(
        QWidget#DiretorDeCulto {
            background: #171717;
            color: #eeeeee;
            font-family: "Segoe UI";
            font-size: 10pt;
        }

        QLabel#mainTitle {
            font-size: 15pt;
            font-weight: 700;
        }

        QLabel#activeStatus {
            color: #49d17d;
            font-weight: 700;
        }

        QGroupBox {
            border: 1px solid #3a3a3a;
            border-radius: 7px;
            margin-top: 10px;
            padding-top: 10px;
            background: #202020;
            font-weight: 600;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
            color: #cfcfcf;
        }

        QLabel#liveScene {
            font-size: 13pt;
            font-weight: 700;
        }

        QLabel#liveBadge {
            color: #ff5757;
            font-weight: 700;
        }

        QLabel#suggestionScene {
            color: #f3a33b;
            font-size: 13pt;
            font-weight: 700;
        }

        QLabel#confidence {
            color: #55d58b;
            font-weight: 700;
        }

        QLabel#reasons {
            color: #cccccc;
            line-height: 140%;
        }

        QPushButton {
            min-height: 34px;
            border-radius: 5px;
            border: 1px solid #4a4a4a;
            background: #2b2b2b;
            color: #ffffff;
            font-weight: 700;
        }

        QPushButton:hover {
            background: #353535;
        }

        QPushButton#cutButton {
            background: #ad3f36;
            border-color: #c14c42;
        }

        QPushButton#cutButton:hover {
            background: #c64d43;
        }

        QProgressBar {
            height: 8px;
            border: 0;
            border-radius: 4px;
            background: #303030;
        }

        QProgressBar::chunk {
            background: #d58b36;
            border-radius: 4px;
        }

        QRadioButton {
            spacing: 7px;
            padding: 3px;
        }
    )"));

    refreshFromObs();
}

QString DiretorDock::currentSceneName() const
{
    obs_source_t *scene = obs_frontend_get_current_scene();
    if (!scene)
        return QString();

    const char *name = obs_source_get_name(scene);
    const QString result = QString::fromUtf8(name ? name : "");
    obs_source_release(scene);
    return result;
}

QString DiretorDock::displaySceneName(const QString &scene) const
{
    if (scene.isEmpty())
        return QStringLiteral("Nenhuma cena");

    return scene;
}

DiretorDock::Suggestion DiretorDock::buildSuggestion(const QString &current) const
{
    Suggestion result;
    const QString n = normalize(current);

    // This alpha uses only information actually available from OBS scene state.
    // It intentionally does not invent camera/face/microphone detections.

    if (n.contains(QStringLiteral("PASTOR")) && !n.contains(QStringLiteral("EDIT"))) {
        result.scene = QStringLiteral("PASTOR EDIT");
        result.confidence = 78;
        result.reasons << QStringLiteral("Cena atual indica enquadramento do pastor")
                       << QStringLiteral("Existe uma versão EDIT/zoom correspondente")
                       << QStringLiteral("Motor de regras evita repetir a mesma cena");
    } else if (n == QStringLiteral("PASTOR EDIT")) {
        result.scene = QStringLiteral("1 - PASTOR");
        result.confidence = 72;
        result.reasons << QStringLiteral("Alternância entre plano normal e EDIT")
                       << QStringLiteral("Reduz permanência excessiva no mesmo enquadramento");
    } else if (n.contains(QStringLiteral("SOLO")) && !n.contains(QStringLiteral("EDIT"))) {
        result.scene = QStringLiteral("SOLO EDIT");
        result.confidence = 78;
        result.reasons << QStringLiteral("Cena atual indica solo")
                       << QStringLiteral("Existe uma versão EDIT/zoom correspondente")
                       << QStringLiteral("Alternância de plano disponível");
    } else if (n == QStringLiteral("SOLO EDIT")) {
        result.scene = QStringLiteral("2 - SOLO");
        result.confidence = 72;
        result.reasons << QStringLiteral("Alternância entre plano normal e EDIT")
                       << QStringLiteral("Evita permanecer indefinidamente no zoom");
    } else if (n == QStringLiteral("CAMERA 2")) {
        result.scene = QStringLiteral("CAM EDIT 2");
        result.confidence = 76;
        result.reasons << QStringLiteral("Existe uma versão EDIT/zoom da câmera 2")
                       << QStringLiteral("Mudança de enquadramento disponível");
    } else if (n == QStringLiteral("CAM EDIT 2")) {
        result.scene = QStringLiteral("CAMERA 2");
        result.confidence = 70;
        result.reasons << QStringLiteral("Alternância entre plano normal e EDIT")
                       << QStringLiteral("Evita repetição do mesmo enquadramento");
    } else if (n.startsWith(QStringLiteral("HINO "))) {
        bool ok = false;
        const int number = n.mid(5).toInt(&ok);
        if (ok && number >= 1 && number < 15) {
            result.scene = QStringLiteral("HINO %1").arg(number + 1);
            result.confidence = 64;
            result.reasons << QStringLiteral("Sequência de HINOS detectada pelo nome da cena")
                           << QStringLiteral("Próxima cena HINO disponível");
        }
    }

    if (result.scene.isEmpty()) {
        // Safe fallback: no automatic action. The operator must choose.
        result.scene = current;
        result.confidence = 0;
        result.reasons << QStringLiteral("Sem evidência suficiente para sugerir outra cena")
                       << QStringLiteral("Motor de visão/áudio ainda não está ativo nesta versão");
    }

    return result;
}

void DiretorDock::refreshFromObs()
{
    updateUi();
    analyze();
}

void DiretorDock::analyze()
{
    elapsedSeconds_++;
    secondsSinceCut_++;

    const QString current = currentSceneName();

    if (current.isEmpty()) {
        setStatus(QStringLiteral("● AGUARDANDO OBS"), false);
        liveSceneLabel_->setText(QStringLiteral("Nenhuma cena disponível"));
        suggestionSceneLabel_->setText(QStringLiteral("Aguardando..."));
        confidenceLabel_->setText(QStringLiteral("Confiança: --"));
        reasonsLabel_->setText(QStringLiteral("O OBS ainda não informou uma cena atual."));
        return;
    }

    setStatus(QStringLiteral("● SISTEMA ATIVO"), true);

    // Only refresh the suggestion periodically so the operator gets a stable
    // decision rather than a constantly changing target.
    secondsSinceAnalysis_++;
    if (secondsSinceAnalysis_ >= kSuggestionEverySeconds || suggestion_.scene.isEmpty()) {
        suggestion_ = buildSuggestion(current);
        secondsSinceAnalysis_ = 0;

        if (suggestion_.scene == lastIgnoredScene_ && suggestion_.confidence > 0) {
            suggestion_.confidence = std::max(0, suggestion_.confidence - 20);
            suggestion_.reasons.prepend(QStringLiteral("Sugestão anterior foi ignorada"));
        }
    }

    if (mode_ == Mode::Automatico &&
        suggestion_.confidence > 0 &&
        suggestion_.scene != current &&
        secondsSinceCut_ >= kMinimumCutIntervalSeconds) {
        applyScene(suggestion_.scene);
    }

    updateUi();
}

void DiretorDock::updateUi()
{
    const QString current = currentSceneName();

    liveSceneLabel_->setText(displaySceneName(current));
    liveBadge_->setText(QStringLiteral("● NO AR"));

    suggestionSceneLabel_->setText(displaySceneName(suggestion_.scene));
    confidenceLabel_->setText(
        suggestion_.confidence > 0
            ? QStringLiteral("Confiança: %1%").arg(suggestion_.confidence)
            : QStringLiteral("Confiança: --"));

    reasonsLabel_->setText(joinReasons(suggestion_.reasons));

    cutButton_->setEnabled(
        !suggestion_.scene.isEmpty() &&
        suggestion_.scene != current &&
        suggestion_.confidence > 0);

    ignoreButton_->setEnabled(suggestion_.confidence > 0);

    const int hours = static_cast<int>(elapsedSeconds_ / 3600);
    const int minutes = static_cast<int>((elapsedSeconds_ % 3600) / 60);
    const int seconds = static_cast<int>(elapsedSeconds_ % 60);

    timeLabel_->setText(
        QStringLiteral("Transmissão: %1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0')));

    cutsLabel_->setText(QStringLiteral("Cortes: %1").arg(cuts_));
    mostUsedLabel_->setText(
        mostUsedScene_.isEmpty()
            ? QStringLiteral("Mais usada: --")
            : QStringLiteral("Mais usada: %1").arg(mostUsedScene_));
}

void DiretorDock::applyScene(const QString &sceneName)
{
    if (sceneName.isEmpty())
        return;

    obs_frontend_source_list scenes = {};
    obs_frontend_get_scenes(&scenes);

    obs_source_t *target = nullptr;
    for (size_t i = 0; i < scenes.sources.num; ++i) {
        obs_source_t *candidate = scenes.sources.array[i];
        const char *name = obs_source_get_name(candidate);

        if (name && sceneName == QString::fromUtf8(name)) {
            target = candidate;
            break;
        }
    }

    if (!target) {
        obs_frontend_source_list_free(&scenes);
        setStatus(QStringLiteral("● CENA NÃO ENCONTRADA"), false);
        return;
    }

    const bool studioMode = obs_frontend_preview_program_mode_active();

    if (studioMode) {
        obs_frontend_set_current_preview_scene(target);
        obs_frontend_preview_program_trigger_transition();
    } else {
        obs_frontend_set_current_scene(target);
    }

    obs_frontend_source_list_free(&scenes);

    cuts_++;
    secondsSinceCut_ = 0;
    secondsSinceAnalysis_ = 0;

    const QString actual = currentSceneName();
    static std::map<QString, int> usage;
    const QString used = actual.isEmpty() ? sceneName : actual;
    const int count = ++usage[used];

    if (count > mostUsedCount_) {
        mostUsedCount_ = count;
        mostUsedScene_ = used;
    }

    updateUi();
}

void DiretorDock::cutSuggestion()
{
    if (suggestion_.confidence <= 0)
        return;

    const QString current = currentSceneName();
    if (suggestion_.scene == current)
        return;

    if (secondsSinceCut_ < kMinimumCutIntervalSeconds) {
        reasonsLabel_->setText(
            QStringLiteral("✓ Corte bloqueado por segurança: intervalo mínimo de %1s.")
                .arg(kMinimumCutIntervalSeconds));
        return;
    }

    applyScene(suggestion_.scene);
}

void DiretorDock::ignoreSuggestion()
{
    lastIgnoredScene_ = suggestion_.scene;
    suggestion_ = buildSuggestion(currentSceneName());

    if (suggestion_.confidence > 0)
        suggestion_.confidence = std::max(0, suggestion_.confidence - 15);

    reasonsLabel_->setText(
        QStringLiteral("Sugestão ignorada. O motor aguardará nova análise."));
    secondsSinceAnalysis_ = 0;
    updateUi();
}

void DiretorDock::modeChanged()
{
    const int id = modeGroup_->checkedId();

    if (id == static_cast<int>(Mode::Manual))
        mode_ = Mode::Manual;
    else if (id == static_cast<int>(Mode::Automatico))
        mode_ = Mode::Automatico;
    else
        mode_ = Mode::Assistido;

    if (mode_ == Mode::Manual) {
        setStatus(QStringLiteral("● MANUAL"), true);
    } else if (mode_ == Mode::Automatico) {
        setStatus(QStringLiteral("● AUTOMÁTICO"), true);
    } else {
        setStatus(QStringLiteral("● ASSISTIDO"), true);
    }

    updateUi();
}

void DiretorDock::setStatus(const QString &text, bool active)
{
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(
        active
            ? QStringLiteral("color:#49d17d;font-weight:700;")
            : QStringLiteral("color:#d5a33c;font-weight:700;"));
}

void DiretorDock::updateProgress()
{
    const int elapsedTenths = static_cast<int>((secondsSinceAnalysis_ * 10) % (kSuggestionEverySeconds * 10));
    const int value = std::clamp(elapsedTenths, 0, kSuggestionEverySeconds * 10);

    progressBar_->setValue(value);

    const int remaining = std::max(0, kSuggestionEverySeconds - static_cast<int>(secondsSinceAnalysis_));
    nextAnalysisLabel_->setText(
        remaining > 0
            ? QStringLiteral("Próxima análise em %1s").arg(remaining)
            : QStringLiteral("Analisando..."));
}
