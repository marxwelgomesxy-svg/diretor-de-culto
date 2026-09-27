#include "diretor-dock.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <media-io/audio-io.h>
#include <media-io/video-io.h>

#include <QBuffer>
#include <QButtonGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <map>

namespace {
constexpr int kAnalysisIntervalMs = 1000;
constexpr int kSuggestionEverySeconds = 5;
constexpr int kMinimumCutIntervalSeconds = 3;
constexpr int kAIAnalysisEverySeconds = 7;
constexpr int kFrameWidth = 320;
constexpr int kFrameHeight = 180;
constexpr size_t kAudioSampleRate = 48000;

QString normalize(const QString &value)
{
    return value.trimmed().toUpper();
}

QString joinReasons(const QStringList &reasons)
{
    if (reasons.isEmpty())
        return QStringLiteral("Nenhuma evidência no momento.");

    QStringList clean;
    for (const QString &reason : reasons) {
        const QString r = reason.trimmed();
        if (!r.isEmpty())
            clean << QStringLiteral("• ") + r;
    }
    return clean.join(QStringLiteral("\n"));
}

QString jsonString(const QJsonObject &obj, const char *key)
{
    return obj.value(QString::fromLatin1(key)).toString().trimmed();
}
}

DiretorDock::DiretorDock(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("DiretorDeCulto"));
    setMinimumWidth(340);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(9, 7, 9, 7);
    root->setSpacing(5);

    auto *titleRow = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("DIRETOR DE CULTO"));
    title->setObjectName(QStringLiteral("mainTitle"));
    statusLabel_ = new QLabel(QStringLiteral("● DESLIGADO"));
    statusLabel_->setObjectName(QStringLiteral("activeStatus"));
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(statusLabel_);
    root->addLayout(titleRow);

    directorToggleButton_ = new QPushButton(QStringLiteral("▶ LIGAR DIRETOR DE CULTO"));
    directorToggleButton_->setObjectName(QStringLiteral("directorToggle"));
    directorToggleButton_->setCheckable(true);
    directorToggleButton_->setMinimumHeight(34);
    root->addWidget(directorToggleButton_);

    auto *liveBox = new QGroupBox(QStringLiteral("CÂMERA / CENA NO AR"));
    auto *liveLayout = new QVBoxLayout(liveBox);
    liveLayout->setContentsMargins(7, 5, 7, 5);
    liveSceneLabel_ = new QLabel(QStringLiteral("Aguardando OBS..."));
    liveSceneLabel_->setObjectName(QStringLiteral("liveScene"));
    liveSceneLabel_->setWordWrap(true);
    liveBadge_ = new QLabel(QStringLiteral(""));
    liveBadge_->setObjectName(QStringLiteral("liveBadge"));
    liveLayout->addWidget(liveSceneLabel_);
    liveLayout->addWidget(liveBadge_);
    root->addWidget(liveBox);

    auto *aiBox = new QGroupBox(QStringLiteral("INTELIGÊNCIA LOCAL — OLLAMA"));
    auto *aiLayout = new QVBoxLayout(aiBox);
    aiLayout->setContentsMargins(7, 5, 7, 5);

    auto *modelRow = new QHBoxLayout();
    auto *modelLabel = new QLabel(QStringLiteral("Modelo:"));
    modelEdit_ = new QLineEdit(QStringLiteral("qwen3-vl:2b"));
    modelEdit_->setToolTip(QStringLiteral("Modelo multimodal local. Não usa chave API."));
    modelRow->addWidget(modelLabel);
    modelRow->addWidget(modelEdit_, 1);
    aiLayout->addLayout(modelRow);

    auto *aiButtons = new QHBoxLayout();
    testAIButton_ = new QPushButton(QStringLiteral("TESTAR IA"));
    installAIButton_ = new QPushButton(QStringLiteral("INSTALAR IA"));
    aiButton_ = new QPushButton(QStringLiteral("ANALISAR AGORA"));
    aiButtons->addWidget(testAIButton_);
    aiButtons->addWidget(installAIButton_);
    aiButtons->addWidget(aiButton_);
    aiLayout->addLayout(aiButtons);

    aiStatusLabel_ = new QLabel(QStringLiteral("● IA LOCAL: aguardando diretor"));
    aiStatusLabel_->setObjectName(QStringLiteral("aiStatus"));
    aiStatusLabel_->setWordWrap(true);
    aiLayout->addWidget(aiStatusLabel_);
    root->addWidget(aiBox);

    auto *suggestionBox = new QGroupBox(QStringLiteral("SUGESTÃO DE CORTE"));
    auto *suggestionLayout = new QVBoxLayout(suggestionBox);
    suggestionLayout->setContentsMargins(7, 5, 7, 5);

    suggestionSceneLabel_ = new QLabel(QStringLiteral("Aguardando análise..."));
    suggestionSceneLabel_->setObjectName(QStringLiteral("suggestionScene"));
    suggestionSceneLabel_->setWordWrap(true);
    confidenceLabel_ = new QLabel(QStringLiteral("Confiança: --"));
    confidenceLabel_->setObjectName(QStringLiteral("confidence"));
    reasonsLabel_ = new QLabel(QStringLiteral("Ligue o Diretor durante a transmissão para começar a análise."));
    reasonsLabel_->setWordWrap(true);
    reasonsLabel_->setObjectName(QStringLiteral("reasons"));
    analysisContextLabel_ = new QLabel(QStringLiteral("Fonte: —"));
    analysisContextLabel_->setObjectName(QStringLiteral("context"));

    suggestionLayout->addWidget(suggestionSceneLabel_);
    suggestionLayout->addWidget(confidenceLabel_);
    suggestionLayout->addWidget(reasonsLabel_);
    suggestionLayout->addWidget(analysisContextLabel_);

    auto *buttons = new QHBoxLayout();
    cutButton_ = new QPushButton(QStringLiteral("CORTAR"));
    ignoreButton_ = new QPushButton(QStringLiteral("IGNORAR"));
    cutButton_->setObjectName(QStringLiteral("cutButton"));
    ignoreButton_->setObjectName(QStringLiteral("ignoreButton"));
    buttons->addWidget(cutButton_, 1);
    buttons->addWidget(ignoreButton_, 1);
    suggestionLayout->addLayout(buttons);
    root->addWidget(suggestionBox);

    auto *telemetryBox = new QGroupBox(QStringLiteral("MONITORAMENTO AO VIVO"));
    auto *telemetryLayout = new QVBoxLayout(telemetryBox);
    telemetryLayout->setContentsMargins(7, 5, 7, 5);
    audioStatusLabel_ = new QLabel(QStringLiteral("Áudio: parado"));
    motionStatusLabel_ = new QLabel(QStringLiteral("Imagem: parada"));
    telemetryLayout->addWidget(audioStatusLabel_);
    telemetryLayout->addWidget(motionStatusLabel_);
    root->addWidget(telemetryBox);

    auto *analysisBox = new QGroupBox(QStringLiteral("PRÓXIMA ANÁLISE"));
    auto *analysisLayout = new QVBoxLayout(analysisBox);
    analysisLayout->setContentsMargins(7, 5, 7, 5);
    progressBar_ = new QProgressBar();
    progressBar_->setRange(0, kSuggestionEverySeconds * 10);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(false);
    nextAnalysisLabel_ = new QLabel(QStringLiteral("Diretor desligado"));
    analysisLayout->addWidget(progressBar_);
    analysisLayout->addWidget(nextAnalysisLabel_);
    root->addWidget(analysisBox);

    auto *modeBox = new QGroupBox(QStringLiteral("MODO DE OPERAÇÃO"));
    auto *modeLayout = new QVBoxLayout(modeBox);
    modeLayout->setContentsMargins(7, 5, 7, 5);
    manualRadio_ = new QRadioButton(QStringLiteral("Manual — não corta sozinho"));
    assistidoRadio_ = new QRadioButton(QStringLiteral("Assistido — sugere e aguarda CORTAR"));
    automaticoRadio_ = new QRadioButton(QStringLiteral("Automático — corta quando atingir a segurança"));
    assistidoRadio_->setChecked(true);
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->addButton(manualRadio_, static_cast<int>(Mode::Manual));
    modeGroup_->addButton(assistidoRadio_, static_cast<int>(Mode::Assistido));
    modeGroup_->addButton(automaticoRadio_, static_cast<int>(Mode::Automatico));
    modeLayout->addWidget(manualRadio_);
    modeLayout->addWidget(assistidoRadio_);
    modeLayout->addWidget(automaticoRadio_);
    root->addWidget(modeBox);

    auto *summaryBox = new QGroupBox(QStringLiteral("RESUMO DA TRANSMISSÃO"));
    auto *summaryLayout = new QVBoxLayout(summaryBox);
    summaryLayout->setContentsMargins(7, 5, 7, 5);
    timeLabel_ = new QLabel(QStringLiteral("Transmissão: 00:00:00"));
    cutsLabel_ = new QLabel(QStringLiteral("Cortes: 0"));
    mostUsedLabel_ = new QLabel(QStringLiteral("Mais usada: --"));
    summaryLayout->addWidget(timeLabel_);
    summaryLayout->addWidget(cutsLabel_);
    summaryLayout->addWidget(mostUsedLabel_);
    root->addWidget(summaryBox);

    network_ = new QNetworkAccessManager(this);

    connect(directorToggleButton_, &QPushButton::clicked, this, &DiretorDock::toggleDirector);
    connect(cutButton_, &QPushButton::clicked, this, &DiretorDock::cutSuggestion);
    connect(ignoreButton_, &QPushButton::clicked, this, &DiretorDock::ignoreSuggestion);
    connect(aiButton_, &QPushButton::clicked, this, &DiretorDock::askLocalAI);
    connect(testAIButton_, &QPushButton::clicked, this, &DiretorDock::testAIConnection);
    connect(installAIButton_, &QPushButton::clicked, this, &DiretorDock::installAI);
    connect(modeGroup_, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int) { modeChanged(); });

    analysisTimer_ = new QTimer(this);
    analysisTimer_->setInterval(kAnalysisIntervalMs);
    connect(analysisTimer_, &QTimer::timeout, this, &DiretorDock::analyze);

    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(100);
    connect(progressTimer_, &QTimer::timeout, this, &DiretorDock::updateProgress);

    setStyleSheet(QStringLiteral(R"(
        QWidget#DiretorDeCulto { background:#171717; color:#eeeeee; font-family:"Segoe UI"; font-size:9pt; }
        QLabel#mainTitle { font-size:14pt; font-weight:800; }
        QLabel#activeStatus { color:#d5a33c; font-weight:800; }
        QGroupBox { border:1px solid #3a3a3a; border-radius:5px; margin-top:7px; padding-top:7px; background:#202020; font-weight:700; }
        QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 4px; color:#cfcfcf; }
        QLabel#liveScene { font-size:13pt; font-weight:800; }
        QLabel#liveBadge { color:#ff4f4f; font-weight:800; }
        QLabel#suggestionScene { color:#f0a13b; font-size:13pt; font-weight:800; }
        QLabel#confidence { color:#58d38b; font-weight:800; }
        QLabel#reasons, QLabel#context { color:#cccccc; }
        QLabel#aiStatus { color:#d5a33c; font-weight:700; }
        QPushButton { min-height:29px; border-radius:4px; border:1px solid #4a4a4a; background:#2b2b2b; color:#ffffff; font-weight:800; padding:0 6px; }
        QPushButton:hover { background:#383838; }
        QPushButton#directorToggle { background:#315c3f; border-color:#4d8c61; }
        QPushButton#directorToggle:checked { background:#8a352f; border-color:#c24b42; }
        QPushButton#cutButton { background:#a83b35; border-color:#c74b41; }
        QPushButton#cutButton:disabled { background:#4a2926; color:#8e8e8e; }
        QPushButton#ignoreButton:disabled, QPushButton:disabled { color:#888888; }
        QLineEdit { min-height:25px; background:#151515; border:1px solid #444444; border-radius:4px; color:#ffffff; padding:0 6px; }
        QProgressBar { height:7px; border:0; border-radius:3px; background:#303030; }
        QProgressBar::chunk { background:#d58b36; border-radius:3px; }
        QRadioButton { spacing:5px; padding:1px; }
    )"));

    refreshFromObs();
}

DiretorDock::~DiretorDock()
{
    stopLoop();
    if (pendingReply_) {
        pendingReply_->abort();
        pendingReply_->deleteLater();
        pendingReply_ = nullptr;
    }
}

void DiretorDock::startLoop()
{
    if (!directorEnabled_ || !streamingActive_)
        return;
    if (!analysisTimer_->isActive())
        analysisTimer_->start();
    if (!progressTimer_->isActive())
        progressTimer_->start();
    startMediaAnalysis();
    nextAnalysisLabel_->setText(QStringLiteral("Próxima análise em %1s").arg(kSuggestionEverySeconds));
    testAIConnection();
}

void DiretorDock::stopLoop()
{
    analysisTimer_->stop();
    progressTimer_->stop();
    stopMediaAnalysis();
    progressBar_->setValue(0);
    nextAnalysisLabel_->setText(directorEnabled_ ? QStringLiteral("Aguardando transmissão") : QStringLiteral("Diretor desligado"));
    if (pendingReply_ && aiBusy_) {
        pendingReply_->abort();
    }
    aiBusy_ = false;
}

void DiretorDock::updateStreamingState()
{
    const bool active = obs_frontend_streaming_active();
    if (active && !previousStreamingState_) {
        elapsedSeconds_ = 0;
        cuts_ = 0;
        mostUsedScene_.clear();
        mostUsedCount_ = 0;
        secondsSinceCut_ = 9999;
        secondsSinceAnalysis_ = 0;
        secondsSinceAI_ = 9999;
        suggestion_ = Suggestion{};
        lastIgnoredScene_.clear();
    }

    streamingActive_ = active;
    previousStreamingState_ = active;

    if (directorEnabled_ && streamingActive_)
        startLoop();
    else
        stopLoop();

    updateUi();
}

void DiretorDock::toggleDirector()
{
    directorEnabled_ = directorToggleButton_->isChecked();
    if (directorEnabled_) {
        directorToggleButton_->setText(QStringLiteral("■ DESLIGAR DIRETOR DE CULTO"));
        setStatus(streamingActive_ ? QStringLiteral("● DIRETOR ATIVO") : QStringLiteral("● AGUARDANDO STREAM"), true);
        if (streamingActive_)
            startLoop();
    } else {
        directorToggleButton_->setText(QStringLiteral("▶ LIGAR DIRETOR DE CULTO"));
        setStatus(QStringLiteral("● DESLIGADO"), false);
        stopLoop();
        suggestion_ = Suggestion{};
        aiLastDecision_.clear();
    }
    updateUi();
}

void DiretorDock::startMediaAnalysis()
{
    if (mediaActive_.exchange(true))
        return;

    struct video_scale_info conversion = {};
    conversion.format = VIDEO_FORMAT_BGRA;
    conversion.width = kFrameWidth;
    conversion.height = kFrameHeight;
    conversion.colorspace = VIDEO_CS_709;
    conversion.range = VIDEO_RANGE_FULL;
    obs_add_raw_video_callback(&conversion, &DiretorDock::rawVideoCallback, this);

    struct audio_convert_info audioConversion = {};
    audioConversion.samples_per_sec = static_cast<uint32_t>(kAudioSampleRate);
    audioConversion.format = AUDIO_FORMAT_FLOAT;
    audioConversion.speakers = SPEAKERS_STEREO;
    obs_add_raw_audio_callback(0, &audioConversion, &DiretorDock::rawAudioCallback, this);
}

void DiretorDock::stopMediaAnalysis()
{
    if (!mediaActive_.exchange(false))
        return;
    obs_remove_raw_video_callback(&DiretorDock::rawVideoCallback, this);
    obs_remove_raw_audio_callback(0, &DiretorDock::rawAudioCallback, this);
    audioRms_.store(0.0f, std::memory_order_relaxed);
    motionScore_.store(0.0f, std::memory_order_relaxed);
    QMutexLocker locker(&frameMutex_);
    latestFrame_ = QImage();
    previousLuma_.clear();
}

void DiretorDock::rawVideoCallback(void *param, struct video_data *frame)
{
    if (param)
        static_cast<DiretorDock *>(param)->processVideoFrame(frame);
}

void DiretorDock::rawAudioCallback(void *param, size_t, struct audio_data *data)
{
    if (param)
        static_cast<DiretorDock *>(param)->processAudio(data);
}

void DiretorDock::processAudio(struct audio_data *data)
{
    if (!data || !data->data[0] || data->frames == 0)
        return;

    const float *samples = reinterpret_cast<const float *>(data->data[0]);
    double sum = 0.0;
    const uint32_t sampleCount = data->frames * 2;
    for (uint32_t i = 0; i < sampleCount; ++i) {
        const double s = samples[i];
        sum += s * s;
    }
    const float rms = static_cast<float>(std::sqrt(sum / std::max<uint32_t>(1, sampleCount)));
    audioRms_.store(std::clamp(rms, 0.0f, 1.0f), std::memory_order_relaxed);
}

void DiretorDock::processVideoFrame(struct video_data *frame)
{
    if (!frame || !frame->data[0])
        return;

    const uint64_t counter = videoFrameCounter_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (counter % 5 != 0)
        return;

    const int width = kFrameWidth;
    const int height = kFrameHeight;
    const int stride = static_cast<int>(frame->linesize[0]);
    if (stride < width * 4)
        return;

    const int stepX = 8;
    const int stepY = 8;
    std::vector<unsigned char> current;
    current.reserve((width / stepX + 1) * (height / stepY + 1));
    double diff = 0.0;
    size_t count = 0;

    for (int y = 0; y < height; y += stepY) {
        const unsigned char *row = frame->data[0] + y * stride;
        for (int x = 0; x < width; x += stepX) {
            const unsigned char *p = row + x * 4;
            const int luma = (29 * p[2] + 150 * p[1] + 77 * p[0]) >> 8;
            const unsigned char value = static_cast<unsigned char>(luma);
            current.push_back(value);
            if (current.size() <= previousLuma_.size())
                diff += std::abs(int(value) - int(previousLuma_[current.size() - 1]));
            ++count;
        }
    }

    if (!previousLuma_.empty() && count > 0) {
        const float score = static_cast<float>(diff / (255.0 * static_cast<double>(count)));
        motionScore_.store(std::clamp(score, 0.0f, 1.0f), std::memory_order_relaxed);
    }
    previousLuma_ = std::move(current);

    if (counter - lastCopiedFrame_ < 30)
        return;

    QImage image(reinterpret_cast<const uchar *>(frame->data[0]), width, height, stride, QImage::Format_ARGB32);
    if (image.isNull())
        return;

    QMutexLocker locker(&frameMutex_);
    latestFrame_ = image.copy();
    lastCopiedFrame_ = counter;
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

QStringList DiretorDock::allSceneNames() const
{
    QStringList names;
    obs_frontend_source_list scenes = {};
    obs_frontend_get_scenes(&scenes);
    for (size_t i = 0; i < scenes.sources.num; ++i) {
        const char *name = obs_source_get_name(scenes.sources.array[i]);
        if (name && *name)
            names << QString::fromUtf8(name);
    }
    obs_frontend_source_list_free(&scenes);
    return names;
}

DiretorDock::Suggestion DiretorDock::buildRuleSuggestion(const QString &current) const
{
    Suggestion result;
    const QString n = normalize(current);

    if (n == QStringLiteral("1 - PASTOR")) {
        result.scene = QStringLiteral("PASTOR EDIT");
        result.confidence = 68;
        result.reasons << QStringLiteral("Existe uma versão EDIT/zoom correspondente");
    } else if (n == QStringLiteral("PASTOR EDIT")) {
        result.scene = QStringLiteral("1 - PASTOR");
        result.confidence = 65;
        result.reasons << QStringLiteral("Alternância disponível entre plano normal e EDIT");
    } else if (n == QStringLiteral("2 - SOLO")) {
        result.scene = QStringLiteral("SOLO EDIT");
        result.confidence = 68;
        result.reasons << QStringLiteral("Existe uma versão EDIT/zoom correspondente");
    } else if (n == QStringLiteral("SOLO EDIT")) {
        result.scene = QStringLiteral("2 - SOLO");
        result.confidence = 65;
        result.reasons << QStringLiteral("Alternância disponível entre plano normal e EDIT");
    } else if (n == QStringLiteral("CAMERA 2")) {
        result.scene = QStringLiteral("CAM EDIT 2");
        result.confidence = 66;
        result.reasons << QStringLiteral("Existe uma versão EDIT/zoom da câmera 2");
    } else if (n == QStringLiteral("CAM EDIT 2")) {
        result.scene = QStringLiteral("CAMERA 2");
        result.confidence = 63;
        result.reasons << QStringLiteral("Alternância disponível entre plano normal e EDIT");
    } else {
        const QRegularExpression hymn(QStringLiteral("^HINO\\s+(\\d+)$"));
        const auto match = hymn.match(n);
        if (match.hasMatch()) {
            const int number = match.captured(1).toInt();
            if (number >= 1 && number < 15) {
                result.scene = QStringLiteral("HINO %1").arg(number + 1);
                result.confidence = 55;
                result.reasons << QStringLiteral("Próxima cena HINO disponível na sequência");
            }
        }
    }

    if (result.scene.isEmpty()) {
        result.scene = current;
        result.confidence = 0;
        result.reasons << QStringLiteral("Sem evidência suficiente para sugerir outra cena");
    }

    if (result.scene == lastIgnoredScene_) {
        result.scene = current;
        result.confidence = 0;
        result.reasons = {QStringLiteral("Sugestão anterior foi ignorada pelo operador")};
        result.source = QStringLiteral("Motor local");
    }
    return result;
}

void DiretorDock::refreshFromObs()
{
    updateStreamingState();
    updateUi();
}

void DiretorDock::analyze()
{
    if (!directorEnabled_ || !streamingActive_)
        return;

    elapsedSeconds_++;
    secondsSinceCut_++;
    secondsSinceAnalysis_++;
    secondsSinceAI_++;

    const QString current = currentSceneName();
    if (current.isEmpty()) {
        setStatus(QStringLiteral("● AGUARDANDO CENA"), false);
        updateUi();
        return;
    }

    setStatus(mode_ == Mode::Automatico ? QStringLiteral("● AUTOMÁTICO")
                                         : mode_ == Mode::Assistido ? QStringLiteral("● ASSISTIDO")
                                                                    : QStringLiteral("● MANUAL"), true);

    if (mode_ == Mode::Manual) {
        suggestion_ = Suggestion{};
    } else if (secondsSinceAnalysis_ >= kSuggestionEverySeconds || suggestion_.scene.isEmpty()) {
        suggestion_ = buildRuleSuggestion(current);
        secondsSinceAnalysis_ = 0;
    }

    if (mode_ != Mode::Manual && secondsSinceAI_ >= kAIAnalysisEverySeconds && !aiBusy_)
        askLocalAI();

    if (mode_ == Mode::Automatico && suggestion_.confidence >= 80 &&
        suggestion_.scene != current && secondsSinceCut_ >= kMinimumCutIntervalSeconds) {
        applyScene(suggestion_.scene);
    }

    updateUi();
}

void DiretorDock::updateUi()
{
    const QString current = currentSceneName();
    liveSceneLabel_->setText(current.isEmpty() ? QStringLiteral("Nenhuma cena") : current);

    if (streamingActive_) {
        liveBadge_->setText(QStringLiteral("● NO AR"));
        liveBadge_->show();
    } else {
        liveBadge_->clear();
        liveBadge_->hide();
    }

    if (!directorEnabled_) {
        suggestionSceneLabel_->setText(QStringLiteral("Diretor desligado"));
        confidenceLabel_->setText(QStringLiteral("Confiança: —"));
        reasonsLabel_->setText(QStringLiteral("Pressione LIGAR DIRETOR DE CULTO para ativar a análise."));
        analysisContextLabel_->setText(QStringLiteral("Fonte: —"));
    } else if (!streamingActive_) {
        suggestionSceneLabel_->setText(QStringLiteral("Aguardando transmissão"));
        confidenceLabel_->setText(QStringLiteral("Confiança: —"));
        reasonsLabel_->setText(QStringLiteral("O Diretor fica em espera até a transmissão começar."));
        analysisContextLabel_->setText(QStringLiteral("Fonte: —"));
    } else {
        suggestionSceneLabel_->setText(suggestion_.scene.isEmpty() ? QStringLiteral("Aguardando análise...") : suggestion_.scene);
        confidenceLabel_->setText(suggestion_.confidence > 0
                                       ? QStringLiteral("Confiança: %1%").arg(suggestion_.confidence)
                                       : QStringLiteral("Confiança: —"));
        reasonsLabel_->setText(joinReasons(suggestion_.reasons));
        analysisContextLabel_->setText(QStringLiteral("Fonte: %1").arg(suggestion_.source));
    }

    const bool validSuggestion = directorEnabled_ && streamingActive_ &&
                                 !suggestion_.scene.isEmpty() && suggestion_.scene != current &&
                                 suggestion_.confidence > 0;
    const bool canCut = validSuggestion && secondsSinceCut_ >= kMinimumCutIntervalSeconds;
    cutButton_->setEnabled(canCut);
    ignoreButton_->setEnabled(validSuggestion);

    const float rms = audioRms_.load(std::memory_order_relaxed);
    const float motion = motionScore_.load(std::memory_order_relaxed);
    const int audioPct = std::clamp(static_cast<int>(rms * 100.0f * 2.5f), 0, 100);
    const int motionPct = std::clamp(static_cast<int>(motion * 100.0f), 0, 100);

    if (streamingActive_ && directorEnabled_) {
        audioStatusLabel_->setText(QStringLiteral("Áudio: %1% %2").arg(audioPct).arg(audioPct > 6 ? QStringLiteral("• sinal ativo") : QStringLiteral("• baixo/silêncio")));
        motionStatusLabel_->setText(QStringLiteral("Imagem: %1% %2").arg(motionPct).arg(motionPct > 12 ? QStringLiteral("• mudança") : QStringLiteral("• estável")));
    } else {
        audioStatusLabel_->setText(QStringLiteral("Áudio: parado"));
        motionStatusLabel_->setText(QStringLiteral("Imagem: parada"));
    }

    const int hours = static_cast<int>(elapsedSeconds_ / 3600);
    const int minutes = static_cast<int>((elapsedSeconds_ % 3600) / 60);
    const int seconds = static_cast<int>(elapsedSeconds_ % 60);
    timeLabel_->setText(QStringLiteral("Transmissão: %1:%2:%3")
                            .arg(hours, 2, 10, QLatin1Char('0'))
                            .arg(minutes, 2, 10, QLatin1Char('0'))
                            .arg(seconds, 2, 10, QLatin1Char('0')));
    cutsLabel_->setText(QStringLiteral("Cortes: %1").arg(cuts_));
    mostUsedLabel_->setText(mostUsedScene_.isEmpty() ? QStringLiteral("Mais usada: --") : QStringLiteral("Mais usada: %1").arg(mostUsedScene_));

    if (!directorEnabled_)
        setStatus(QStringLiteral("● DESLIGADO"), false);
    else if (!streamingActive_)
        setStatus(QStringLiteral("● AGUARDANDO STREAM"), true);
}

void DiretorDock::applyScene(const QString &sceneName)
{
    if (sceneName.isEmpty() || !streamingActive_ || !directorEnabled_)
        return;

    const QString current = currentSceneName();
    if (sceneName == current)
        return;

    obs_frontend_source_list scenes = {};
    obs_frontend_get_scenes(&scenes);
    obs_source_t *target = nullptr;
    for (size_t i = 0; i < scenes.sources.num; ++i) {
        const char *name = obs_source_get_name(scenes.sources.array[i]);
        if (name && sceneName == QString::fromUtf8(name)) {
            target = scenes.sources.array[i];
            break;
        }
    }

    if (!target) {
        obs_frontend_source_list_free(&scenes);
        reasonsLabel_->setText(QStringLiteral("Cena não encontrada no OBS: %1").arg(sceneName));
        return;
    }

    if (obs_frontend_preview_program_mode_active()) {
        obs_frontend_set_current_preview_scene(target);
        obs_frontend_preview_program_trigger_transition();
    } else {
        obs_frontend_set_current_scene(target);
    }
    obs_frontend_source_list_free(&scenes);

    cuts_++;
    secondsSinceCut_ = 0;
    secondsSinceAI_ = kAIAnalysisEverySeconds;
    const QString used = sceneName;
    static std::map<QString, int> usage;
    const int count = ++usage[used];
    if (count > mostUsedCount_) {
        mostUsedCount_ = count;
        mostUsedScene_ = used;
    }

    suggestion_ = Suggestion{};
    secondsSinceAnalysis_ = 0;
    updateUi();
}

void DiretorDock::cutSuggestion()
{
    if (!directorEnabled_ || !streamingActive_)
        return;
    const QString current = currentSceneName();
    if (suggestion_.scene.isEmpty() || suggestion_.scene == current || suggestion_.confidence <= 0)
        return;
    if (secondsSinceCut_ < kMinimumCutIntervalSeconds) {
        reasonsLabel_->setText(QStringLiteral("Corte protegido: aguarde %1s entre cortes.").arg(kMinimumCutIntervalSeconds - secondsSinceCut_));
        return;
    }
    applyScene(suggestion_.scene);
}

void DiretorDock::ignoreSuggestion()
{
    if (!directorEnabled_ || !streamingActive_ || suggestion_.scene.isEmpty() || suggestion_.scene == currentSceneName())
        return;

    lastIgnoredScene_ = suggestion_.scene;
    const QString current = currentSceneName();
    suggestion_ = buildRuleSuggestion(current);
    if (suggestion_.scene == lastIgnoredScene_ || suggestion_.scene == current) {
        suggestion_.scene = current;
        suggestion_.confidence = 0;
        suggestion_.reasons = {QStringLiteral("Sugestão ignorada. Aguardando nova evidência.")};
    }
    suggestion_.source = QStringLiteral("Operador + motor local");
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

    if (mode_ == Mode::Manual)
        suggestion_ = Suggestion{};

    updateUi();
}

void DiretorDock::setStatus(const QString &text, bool active)
{
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(active
                                    ? QStringLiteral("color:#49d17d;font-weight:800;")
                                    : QStringLiteral("color:#d5a33c;font-weight:800;"));
}

void DiretorDock::setAIStatus(const QString &text, bool online)
{
    aiOnline_ = online;
    aiStatusLabel_->setText(text);
    aiStatusLabel_->setStyleSheet(online
                                      ? QStringLiteral("color:#49d17d;font-weight:800;")
                                      : QStringLiteral("color:#d5a33c;font-weight:800;"));
}

void DiretorDock::updateProgress()
{
    if (!directorEnabled_ || !streamingActive_) {
        progressBar_->setValue(0);
        return;
    }

    const int elapsedTenths = static_cast<int>((secondsSinceAnalysis_ * 10) % (kSuggestionEverySeconds * 10));
    progressBar_->setValue(std::clamp(elapsedTenths, 0, kSuggestionEverySeconds * 10));
    const int remaining = std::max(0, kSuggestionEverySeconds - static_cast<int>(secondsSinceAnalysis_));
    nextAnalysisLabel_->setText(remaining > 0 ? QStringLiteral("Próxima análise em %1s").arg(remaining)
                                              : QStringLiteral("Analisando..."));
}

QString DiretorDock::buildAIPrompt(const QString &current) const
{
    const QStringList scenes = allSceneNames();
    const float audio = audioRms_.load(std::memory_order_relaxed);
    const float motion = motionScore_.load(std::memory_order_relaxed);

    QString prompt;
    prompt += QStringLiteral("Você é o DIRETOR DE CULTO de uma transmissão ao vivo.\n");
    prompt += QStringLiteral("Escolha somente uma cena da lista. Nunca invente nome de cena.\n");
    prompt += QStringLiteral("Cena atual: ") + current + QStringLiteral("\n");
    prompt += QStringLiteral("Áudio RMS: ") + QString::number(audio, 'f', 3) + QStringLiteral("\n");
    prompt += QStringLiteral("Movimento visual: ") + QString::number(motion, 'f', 3) + QStringLiteral("\n");
    prompt += QStringLiteral("Cenas disponíveis:\n- ") + scenes.join(QStringLiteral("\n- ")) + QStringLiteral("\n\n");
    prompt += QStringLiteral("Analise a imagem atual: pessoas, enquadramento, pastor, solo, regente, instrumentos, grupo e contexto.\n");
    prompt += QStringLiteral("Considere áudio e movimento como sinais auxiliares. Não troque de cena apenas por tempo.\n");
    prompt += QStringLiteral("Só recomende corte se houver evidência clara.\n");
    prompt += QStringLiteral("Retorne SOMENTE JSON: {\"scene\":\"NOME EXATO\",\"confidence\":0,\"reasons\":[\"motivo curto\"]}.\n");
    prompt += QStringLiteral("Se não houver evidência, use a cena atual e confidence 0.\n");
    return prompt;
}

void DiretorDock::testAIConnection()
{
    if (!directorEnabled_ || !streamingActive_ || pendingReply_)
        return;

    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:11434/api/tags")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    pendingReply_ = network_->get(request);
    connect(pendingReply_, &QNetworkReply::finished, this, &DiretorDock::onAIReply);
    setAIStatus(QStringLiteral("● IA LOCAL: verificando Ollama..."), false);
}

void DiretorDock::installAI()
{
    if (!directorEnabled_) {
        setAIStatus(QStringLiteral("● IA LOCAL: ligue o Diretor primeiro"), false);
        return;
    }

    const QString model = modelEdit_->text().trimmed();
    if (model.isEmpty()) {
        setAIStatus(QStringLiteral("● IA LOCAL: informe o modelo"), false);
        return;
    }

    const bool started = QProcess::startDetached(QStringLiteral("ollama"), QStringList{QStringLiteral("pull"), model});
    if (started) {
        setAIStatus(QStringLiteral("● IA LOCAL: baixando %1 — aguarde e teste novamente").arg(model), false);
    } else {
        setAIStatus(QStringLiteral("● IA LOCAL: Ollama não encontrado no Windows. Instale o Ollama e tente novamente."), false);
    }
}

void DiretorDock::askLocalAI()
{
    if (!directorEnabled_ || !streamingActive_ || aiBusy_ || pendingReply_)
        return;

    QImage frame;
    {
        QMutexLocker locker(&frameMutex_);
        frame = latestFrame_.copy();
    }
    if (frame.isNull()) {
        setAIStatus(QStringLiteral("● IA LOCAL: aguardando vídeo do OBS"), false);
        return;
    }

    const QString model = modelEdit_->text().trimmed();
    if (model.isEmpty()) {
        setAIStatus(QStringLiteral("● IA LOCAL: informe um modelo"), false);
        return;
    }

    QByteArray imageBytes;
    QBuffer buffer(&imageBytes);
    buffer.open(QIODevice::WriteOnly);
    frame.save(&buffer, "JPG", 72);
    buffer.close();

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("prompt"), buildAIPrompt(currentSceneName()));
    body.insert(QStringLiteral("stream"), false);
    body.insert(QStringLiteral("format"), QStringLiteral("json"));
    body.insert(QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), 0.1}});
    body.insert(QStringLiteral("images"), QJsonArray{QString::fromLatin1(imageBytes.toBase64())});

    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:11434/api/generate")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    pendingReply_ = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    aiBusy_ = true;
    secondsSinceAI_ = 0;
    connect(pendingReply_, &QNetworkReply::finished, this, &DiretorDock::onAIReply);
    setAIStatus(QStringLiteral("● IA LOCAL: analisando imagem + áudio + cenas..."), false);
}

void DiretorDock::onAIReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        reply = pendingReply_;
    if (!reply)
        return;

    const QByteArray raw = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    reply->deleteLater();
    if (reply == pendingReply_)
        pendingReply_ = nullptr;
    aiBusy_ = false;

    if (error != QNetworkReply::NoError) {
        if (directorEnabled_ && streamingActive_) {
            // Tenta iniciar o servidor local se o Ollama estiver instalado e ainda não estiver rodando.
                QProcess::startDetached(QStringLiteral("ollama"), QStringList{QStringLiteral("serve")});
            QTimer::singleShot(2500, this, &DiretorDock::testAIConnection);
        }
        setAIStatus(QStringLiteral("● IA LOCAL: offline. Use INSTALAR IA se o modelo ainda não estiver instalado."), false);
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument outer = QJsonDocument::fromJson(raw, &parseError);
    if (!outer.isObject()) {
        setAIStatus(QStringLiteral("● IA LOCAL: resposta inválida"), false);
        return;
    }

    if (outer.object().contains(QStringLiteral("models"))) {
        const QJsonArray models = outer.object().value(QStringLiteral("models")).toArray();
        const QString wanted = modelEdit_->text().trimmed();
        bool found = false;
        for (const QJsonValue &value : models) {
            const QString name = value.toObject().value(QStringLiteral("name")).toString();
            if (name == wanted || name.startsWith(wanted + QStringLiteral(":"))) {
                found = true;
                break;
            }
        }
        if (found)
            setAIStatus(QStringLiteral("● IA LOCAL: Ollama conectado • %1 pronto").arg(wanted), true);
        else
            setAIStatus(QStringLiteral("● IA LOCAL: Ollama conectado, mas %1 não está instalado").arg(wanted), false);
        return;
    }

    QString content = outer.object().value(QStringLiteral("response")).toString().trimmed();
    if (content.isEmpty()) {
        setAIStatus(QStringLiteral("● IA LOCAL: modelo não retornou decisão"), false);
        return;
    }

    QJsonParseError innerError{};
    QJsonDocument decisionDoc = QJsonDocument::fromJson(content.toUtf8(), &innerError);
    if (!decisionDoc.isObject()) {
        setAIStatus(QStringLiteral("● IA LOCAL: JSON de decisão inválido"), false);
        return;
    }

    const QJsonObject decision = decisionDoc.object();
    const QString target = jsonString(decision, "scene");
    const int confidence = decision.value(QStringLiteral("confidence")).toInt(0);
    QStringList reasons;
    const QJsonArray arr = decision.value(QStringLiteral("reasons")).toArray();
    for (const QJsonValue &value : arr) {
        const QString reason = value.toString().trimmed();
        if (!reason.isEmpty())
            reasons << reason;
    }

    const QStringList scenes = allSceneNames();
    const QString current = currentSceneName();
    if (target.isEmpty() || !scenes.contains(target) || target == current || target == lastIgnoredScene_) {
        suggestion_.scene = current;
        suggestion_.confidence = 0;
        suggestion_.reasons = {target == lastIgnoredScene_
                                   ? QStringLiteral("A IA repetiu uma cena ignorada; aguardando nova evidência.")
                                   : QStringLiteral("IA não encontrou evidência suficiente para outro corte")};
        suggestion_.source = QStringLiteral("IA local");
        setAIStatus(QStringLiteral("● IA LOCAL: conectada"), true);
        updateUi();
        return;
    }

    suggestion_.scene = target;
    suggestion_.confidence = std::clamp(confidence, 0, 100);
    suggestion_.reasons = reasons.isEmpty() ? QStringList{QStringLiteral("Decisão visual/contextual da IA local")} : reasons;
    suggestion_.source = QStringLiteral("IA local + sensores OBS");
    aiLastDecision_ = target;
    setAIStatus(QStringLiteral("● IA LOCAL: conectada • sugestão: %1").arg(target), true);
    updateUi();
}
