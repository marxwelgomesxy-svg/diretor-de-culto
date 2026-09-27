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
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace {
constexpr int kAnalysisIntervalMs = 1000;
constexpr int kSuggestionEverySeconds = 5;
constexpr int kMinimumCutIntervalSeconds = 5;
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
    QString result;
    for (const QString &reason : reasons)
        result += QStringLiteral("• ") + reason + QStringLiteral("\n");
    return result.trimmed();
}

QLabel *makeTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

QString jsonString(const QJsonObject &obj, const char *key)
{
    return obj.value(QString::fromLatin1(key)).toString().trimmed();
}
}

DiretorDock::DiretorDock(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("DiretorDeCulto"));
    setMinimumWidth(360);
    setMinimumHeight(720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(7);

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

    auto *aiBox = new QGroupBox(QStringLiteral("INTELIGÊNCIA LOCAL"));
    auto *aiLayout = new QVBoxLayout(aiBox);
    auto *modelRow = new QHBoxLayout();
    modelRow->addWidget(new QLabel(QStringLiteral("Modelo:")));
    modelEdit_ = new QLineEdit(QStringLiteral("qwen3-vl:2b"));
    modelEdit_->setToolTip(QStringLiteral("Modelo Ollama local. Sem chave API."));
    modelRow->addWidget(modelEdit_);
    aiLayout->addLayout(modelRow);
    auto *aiButtons = new QHBoxLayout();
    testAIButton_ = new QPushButton(QStringLiteral("TESTAR IA"));
    aiButton_ = new QPushButton(QStringLiteral("ANALISAR AGORA"));
    aiButtons->addWidget(testAIButton_);
    aiButtons->addWidget(aiButton_);
    aiLayout->addLayout(aiButtons);
    aiStatusLabel_ = new QLabel(QStringLiteral("● IA LOCAL: não testada"));
    aiStatusLabel_->setObjectName(QStringLiteral("aiStatus"));
    aiLayout->addWidget(aiStatusLabel_);
    root->addWidget(aiBox);

    auto *suggestionBox = new QGroupBox(QStringLiteral("SUGESTÃO DE CORTE"));
    auto *suggestionLayout = new QVBoxLayout(suggestionBox);
    suggestionSceneLabel_ = new QLabel(QStringLiteral("Aguardando análise..."));
    suggestionSceneLabel_->setObjectName(QStringLiteral("suggestionScene"));
    confidenceLabel_ = new QLabel(QStringLiteral("Confiança: --"));
    confidenceLabel_->setObjectName(QStringLiteral("confidence"));
    reasonsLabel_ = new QLabel(QStringLiteral("Aguardando o Diretor analisar as cenas."));
    reasonsLabel_->setWordWrap(true);
    reasonsLabel_->setObjectName(QStringLiteral("reasons"));
    analysisContextLabel_ = new QLabel(QStringLiteral("Fonte: motor local"));
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
    buttons->addWidget(cutButton_);
    buttons->addWidget(ignoreButton_);
    suggestionLayout->addLayout(buttons);
    root->addWidget(suggestionBox);

    auto *telemetryBox = new QGroupBox(QStringLiteral("MONITORAMENTO AO VIVO"));
    auto *telemetryLayout = new QVBoxLayout(telemetryBox);
    audioStatusLabel_ = new QLabel(QStringLiteral("Microfone/áudio: aguardando dados"));
    motionStatusLabel_ = new QLabel(QStringLiteral("Movimento da imagem: aguardando dados"));
    telemetryLayout->addWidget(audioStatusLabel_);
    telemetryLayout->addWidget(motionStatusLabel_);
    root->addWidget(telemetryBox);

    auto *analysisBox = new QGroupBox(QStringLiteral("PRÓXIMA ANÁLISE"));
    auto *analysisLayout = new QVBoxLayout(analysisBox);
    progressBar_ = new QProgressBar();
    progressBar_->setRange(0, kSuggestionEverySeconds * 10);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(false);
    nextAnalysisLabel_ = new QLabel(QStringLiteral("Próxima análise em 5s"));
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

    network_ = new QNetworkAccessManager(this);

    connect(cutButton_, &QPushButton::clicked, this, &DiretorDock::cutSuggestion);
    connect(ignoreButton_, &QPushButton::clicked, this, &DiretorDock::ignoreSuggestion);
    connect(aiButton_, &QPushButton::clicked, this, &DiretorDock::askLocalAI);
    connect(testAIButton_, &QPushButton::clicked, this, &DiretorDock::testAIConnection);
    connect(modeGroup_, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int) { modeChanged(); });

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
        QWidget#DiretorDeCulto { background:#171717; color:#eeeeee; font-family:"Segoe UI"; font-size:10pt; }
        QLabel#mainTitle { font-size:15pt; font-weight:800; }
        QLabel#activeStatus { color:#49d17d; font-weight:700; }
        QGroupBox { border:1px solid #393939; border-radius:6px; margin-top:8px; padding-top:9px; background:#202020; font-weight:700; }
        QGroupBox::title { subcontrol-origin:margin; left:9px; padding:0 5px; color:#cfcfcf; }
        QLabel#liveScene { font-size:15pt; font-weight:800; }
        QLabel#liveBadge { color:#ff4f4f; font-weight:800; }
        QLabel#suggestionScene { color:#f0a13b; font-size:15pt; font-weight:800; }
        QLabel#confidence { color:#58d38b; font-weight:800; }
        QLabel#reasons, QLabel#context { color:#cccccc; }
        QLabel#aiStatus { color:#d5a33c; font-weight:700; }
        QPushButton { min-height:34px; border-radius:5px; border:1px solid #4a4a4a; background:#2b2b2b; color:#ffffff; font-weight:800; padding:0 9px; }
        QPushButton:hover { background:#383838; }
        QPushButton#cutButton { background:#b43f36; border-color:#c74b41; }
        QPushButton#cutButton:disabled { background:#4a2926; color:#999999; }
        QLineEdit { min-height:28px; background:#151515; border:1px solid #444444; border-radius:4px; color:#ffffff; padding:0 7px; }
        QProgressBar { height:8px; border:0; border-radius:4px; background:#303030; }
        QProgressBar::chunk { background:#d58b36; border-radius:4px; }
        QRadioButton { spacing:7px; padding:3px; }
    )"));

    startMediaAnalysis();
    refreshFromObs();
}

DiretorDock::~DiretorDock()
{
    stopMediaAnalysis();
    if (pendingReply_) {
        pendingReply_->abort();
        pendingReply_->deleteLater();
        pendingReply_ = nullptr;
    }
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
    if (n.contains(QStringLiteral("PASTOR")) && !n.contains(QStringLiteral("EDIT"))) {
        result.scene = QStringLiteral("PASTOR EDIT");
        result.confidence = 68;
        result.reasons << QStringLiteral("Existe uma versão EDIT/zoom correspondente");
    } else if (n == QStringLiteral("PASTOR EDIT")) {
        result.scene = QStringLiteral("1 - PASTOR");
        result.confidence = 65;
        result.reasons << QStringLiteral("Alternância disponível entre plano normal e EDIT");
    } else if (n.contains(QStringLiteral("SOLO")) && !n.contains(QStringLiteral("EDIT"))) {
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
    }
    if (result.scene.isEmpty()) {
        result.scene = current;
        result.confidence = 0;
        result.reasons << QStringLiteral("Sem evidência suficiente para sugerir outra cena");
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
    secondsSinceAnalysis_++;
    secondsSinceAI_++;

    const QString current = currentSceneName();
    if (current.isEmpty()) {
        setStatus(QStringLiteral("● AGUARDANDO OBS"), false);
        liveSceneLabel_->setText(QStringLiteral("Nenhuma cena disponível"));
        return;
    }

    setStatus(QStringLiteral("● SISTEMA ATIVO"), true);
    if (secondsSinceAnalysis_ >= kSuggestionEverySeconds || suggestion_.scene.isEmpty()) {
        suggestion_ = buildRuleSuggestion(current);
        secondsSinceAnalysis_ = 0;
    }

    if (secondsSinceAI_ >= kAIAnalysisEverySeconds && !aiBusy_)
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
    liveBadge_->setText(QStringLiteral("● NO AR"));

    suggestionSceneLabel_->setText(suggestion_.scene.isEmpty() ? QStringLiteral("Aguardando...") : suggestion_.scene);
    confidenceLabel_->setText(suggestion_.confidence > 0
                                   ? QStringLiteral("Confiança: %1%").arg(suggestion_.confidence)
                                   : QStringLiteral("Confiança: --"));
    reasonsLabel_->setText(joinReasons(suggestion_.reasons));
    analysisContextLabel_->setText(QStringLiteral("Fonte: %1").arg(suggestion_.source));

    cutButton_->setEnabled(!suggestion_.scene.isEmpty() && suggestion_.scene != current && suggestion_.confidence > 0);
    ignoreButton_->setEnabled(suggestion_.confidence > 0);

    const float rms = audioRms_.load(std::memory_order_relaxed);
    const float motion = motionScore_.load(std::memory_order_relaxed);
    const int audioPct = std::clamp(static_cast<int>(rms * 100.0f * 2.5f), 0, 100);
    const int motionPct = std::clamp(static_cast<int>(motion * 100.0f), 0, 100);
    audioStatusLabel_->setText(QStringLiteral("Microfone/áudio: %1% %2").arg(audioPct).arg(audioPct > 6 ? QStringLiteral("• sinal ativo") : QStringLiteral("• silêncio/baixo")));
    motionStatusLabel_->setText(QStringLiteral("Movimento da imagem: %1% %2").arg(motionPct).arg(motionPct > 12 ? QStringLiteral("• mudança detectada") : QStringLiteral("• estável")));

    const int hours = static_cast<int>(elapsedSeconds_ / 3600);
    const int minutes = static_cast<int>((elapsedSeconds_ % 3600) / 60);
    const int seconds = static_cast<int>(elapsedSeconds_ % 60);
    timeLabel_->setText(QStringLiteral("Transmissão: %1:%2:%3")
                            .arg(hours, 2, 10, QLatin1Char('0'))
                            .arg(minutes, 2, 10, QLatin1Char('0'))
                            .arg(seconds, 2, 10, QLatin1Char('0')));
    cutsLabel_->setText(QStringLiteral("Cortes: %1").arg(cuts_));
    mostUsedLabel_->setText(mostUsedScene_.isEmpty() ? QStringLiteral("Mais usada: --") : QStringLiteral("Mais usada: %1").arg(mostUsedScene_));
}

void DiretorDock::applyScene(const QString &sceneName)
{
    if (sceneName.isEmpty())
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
        setStatus(QStringLiteral("● CENA NÃO ENCONTRADA"), false);
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
    const QString actual = currentSceneName();
    const QString used = actual.isEmpty() ? sceneName : actual;
    static std::map<QString, int> usage;
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
        reasonsLabel_->setText(QStringLiteral("Corte protegido: aguarde %1s entre cortes.").arg(kMinimumCutIntervalSeconds));
        return;
    }
    applyScene(suggestion_.scene);
}

void DiretorDock::ignoreSuggestion()
{
    lastIgnoredScene_ = suggestion_.scene;
    suggestion_ = buildRuleSuggestion(currentSceneName());
    suggestion_.confidence = std::max(0, suggestion_.confidence - 20);
    suggestion_.reasons.prepend(QStringLiteral("Sugestão anterior ignorada pelo operador"));
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
        setStatus(QStringLiteral("● MANUAL"), true);
    else if (mode_ == Mode::Automatico)
        setStatus(QStringLiteral("● AUTOMÁTICO"), true);
    else
        setStatus(QStringLiteral("● ASSISTIDO"), true);
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
    const int elapsedTenths = static_cast<int>((secondsSinceAnalysis_ * 10) % (kSuggestionEverySeconds * 10));
    progressBar_->setValue(std::clamp(elapsedTenths, 0, kSuggestionEverySeconds * 10));
    const int remaining = std::max(0, kSuggestionEverySeconds - static_cast<int>(secondsSinceAnalysis_));
    nextAnalysisLabel_->setText(remaining > 0 ? QStringLiteral("Próxima análise em %1s").arg(remaining) : QStringLiteral("Analisando...") );
}

QString DiretorDock::buildAIPrompt(const QString &current) const
{
    const QStringList scenes = allSceneNames();
    const float audio = audioRms_.load(std::memory_order_relaxed);
    const float motion = motionScore_.load(std::memory_order_relaxed);

    QString prompt;
    prompt += QStringLiteral("Você é o DIRETOR DE CULTO de uma transmissão ao vivo.\n");
    prompt += QStringLiteral("Escolha somente uma cena da lista fornecida. Nunca invente nome de cena.\n");
    prompt += QStringLiteral("A cena atual é: ") + current + QStringLiteral("\n");
    prompt += QStringLiteral("Áudio RMS aproximado: ") + QString::number(audio, 'f', 3) + QStringLiteral("\n");
    prompt += QStringLiteral("Movimento visual aproximado: ") + QString::number(motion, 'f', 3) + QStringLiteral("\n");
    prompt += QStringLiteral("Cenas disponíveis:\n- ") + scenes.join(QStringLiteral("\n- ")) + QStringLiteral("\n\n");
    prompt += QStringLiteral("Se houver imagem, analise pessoas, enquadramento, apresentação, movimento, instrumentos, pastor, regente e contexto visual.\n");
    prompt += QStringLiteral("Considere também áudio e permanência na cena. Não faça corte só porque passou tempo.\n");
    prompt += QStringLiteral("Retorne SOMENTE JSON neste formato: {\"scene\":\"NOME EXATO\",\"confidence\":0,\"reasons\":[\"motivo\"]}.\n");
    prompt += QStringLiteral("Se não houver evidência suficiente, use a própria cena atual e confidence 0.\n");
    return prompt;
}

void DiretorDock::testAIConnection()
{
    if (pendingReply_)
        return;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:11434/api/tags")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    pendingReply_ = network_->get(request);
    connect(pendingReply_, &QNetworkReply::finished, this, &DiretorDock::onAIReply);
    setAIStatus(QStringLiteral("● IA LOCAL: verificando Ollama..."), false);
}

void DiretorDock::askLocalAI()
{
    if (aiBusy_ || pendingReply_)
        return;

    QImage frame;
    {
        QMutexLocker locker(&frameMutex_);
        frame = latestFrame_.copy();
    }
    if (frame.isNull()) {
        setAIStatus(QStringLiteral("● IA LOCAL: aguardando frame do OBS"), false);
        return;
    }

    const QString model = modelEdit_->text().trimmed();
    if (model.isEmpty()) {
        setAIStatus(QStringLiteral("● IA LOCAL: informe um modelo Ollama"), false);
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
    body.insert(QStringLiteral("images"), QJsonArray{QString::fromLatin1(imageBytes.toBase64())});

    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:11434/api/generate")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    pendingReply_ = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    aiBusy_ = true;
    secondsSinceAI_ = 0;
    connect(pendingReply_, &QNetworkReply::finished, this, &DiretorDock::onAIReply);
    setAIStatus(QStringLiteral("● IA LOCAL: analisando vídeo + áudio + cenas..."), false);
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
        setAIStatus(QStringLiteral("● IA LOCAL: offline — inicie o Ollama"), false);
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument outer = QJsonDocument::fromJson(raw, &parseError);
    if (!outer.isObject()) {
        setAIStatus(QStringLiteral("● IA LOCAL: resposta inválida"), false);
        return;
    }

    if (outer.object().contains(QStringLiteral("models"))) {
        setAIStatus(QStringLiteral("● IA LOCAL: Ollama conectado"), true);
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
    if (target.isEmpty() || !scenes.contains(target) || target == current) {
        suggestion_.scene = current;
        suggestion_.confidence = 0;
        suggestion_.reasons = QStringList{QStringLiteral("IA não encontrou evidência suficiente para outro corte")};
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
    setAIStatus(QStringLiteral("● IA LOCAL: conectada • última decisão: %1").arg(target), true);
    updateUi();
}
