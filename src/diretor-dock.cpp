#include "diretor-dock.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <media-io/audio-io.h>
#include <media-io/video-io.h>
#include <graphics/graphics.h>

#include <QBuffer>
#include <QButtonGroup>
#include <QComboBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QMetaObject>
#include <QVBoxLayout>
#include <QFont>
#include <QPixmap>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace {
constexpr int kAnalysisIntervalMs = 1000;
constexpr int kSuggestionEverySeconds = 4;
constexpr int kMinimumCutIntervalSeconds = 3;
constexpr int kAIAnalysisEverySeconds = 7;
constexpr int kAIErrorCooldownSeconds = 30;
constexpr int kMaxRecentScenes = 8;
constexpr int kCandidateCaptureIntervalMs = 2500;
constexpr int kFrameWidth = 320;
constexpr int kFrameHeight = 180;
constexpr size_t kAudioSampleRate = 48000;
constexpr size_t kAudioChannels = 2;
constexpr size_t kAudioKeepSeconds = 3;

QString normalize(const QString &value)
{
    return value.trimmed().toUpper();
}

QString joinReasons(const QStringList &reasons)
{
    if (reasons.isEmpty())
        return QStringLiteral("Aguardando evidência visual e sonora.");
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

void appendLE16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
}

void appendLE32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 24) & 0xff));
}

#ifdef _WIN32
struct WinHttpResult {
    int status = 0;
    QString error;
    QByteArray body;
};

WinHttpResult postGeminiWinHttp(const QString &url, const QByteArray &body, const QByteArray &apiKey)
{
    WinHttpResult result;
    const std::wstring urlW = url.toStdWString();
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t path[4096]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(urlW.c_str(), 0, 0, &parts)) {
        result.error = QStringLiteral("WinHTTP: WinHttpCrackUrl falhou (%1)").arg(GetLastError());
        return result;
    }
    HINTERNET session = WinHttpOpen(L"DiretorDeCulto/5.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.error = QStringLiteral("WinHTTP: WinHttpOpen falhou (%1)").arg(GetLastError());
        return result;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 30000, 30000);
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (!connection) {
        result.error = QStringLiteral("WinHTTP: WinHttpConnect falhou (%1)").arg(GetLastError());
        WinHttpCloseHandle(session);
        return result;
    }
    const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        result.error = QStringLiteral("WinHTTP: WinHttpOpenRequest falhou (%1)").arg(GetLastError());
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }
    const std::wstring apiKeyW = QString::fromUtf8(apiKey).toStdWString();
    const std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\nx-goog-api-key: " + apiKeyW + L"\r\n";
    const BOOL sent = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L),
                                         const_cast<char *>(body.constData()), static_cast<DWORD>(body.size()),
                                         static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        result.error = QStringLiteral("WinHTTP: requisição HTTPS falhou (%1)").arg(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        result.status = static_cast<int>(status);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            result.error = QStringLiteral("WinHTTP: leitura da resposta falhou (%1)").arg(GetLastError());
            break;
        }
        if (available == 0) break;
        QByteArray chunk;
        chunk.resize(static_cast<int>(available));
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) {
            result.error = QStringLiteral("WinHTTP: WinHttpReadData falhou (%1)").arg(GetLastError());
            break;
        }
        chunk.resize(static_cast<int>(read));
        result.body += chunk;
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}
#endif
}

DiretorDock::DiretorDock(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("DiretorDeCulto"));
    setMinimumWidth(390);

    QSettings settings(QStringLiteral("DiretorDeCulto"), QStringLiteral("OBS"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(5);

    auto *header = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("DIRETOR DE CULTO"));
    title->setObjectName(QStringLiteral("mainTitle"));
    statusLabel_ = new QLabel(QStringLiteral("● DESLIGADO"));
    statusLabel_->setObjectName(QStringLiteral("activeStatus"));
    header->addWidget(title);
    header->addStretch();
    header->addWidget(statusLabel_);
    root->addLayout(header);

    directorToggleButton_ = new QPushButton(QStringLiteral("▶  LIGAR DIRETOR DE CULTO"));
    directorToggleButton_->setObjectName(QStringLiteral("directorToggle"));
    directorToggleButton_->setCheckable(true);
    directorToggleButton_->setMinimumHeight(36);
    root->addWidget(directorToggleButton_);

    auto *liveBox = new QGroupBox(QStringLiteral("CÂMERA NO AR"));
    auto *liveLayout = new QVBoxLayout(liveBox);
    liveLayout->setContentsMargins(7, 5, 7, 6);
    liveSceneLabel_ = new QLabel(QStringLiteral("Nenhuma cena selecionada"));
    liveSceneLabel_->setObjectName(QStringLiteral("liveScene"));
    liveSceneLabel_->setWordWrap(true);
    liveBadge_ = new QLabel();
    liveBadge_->setObjectName(QStringLiteral("liveBadge"));
    liveBadge_->hide();
    livePreview_ = new QLabel(QStringLiteral("PRÉVIA DO PROGRAMA"));
    livePreview_->setObjectName(QStringLiteral("livePreview"));
    livePreview_->setFixedHeight(180);
    livePreview_->setMinimumWidth(320);
    livePreview_->setAlignment(Qt::AlignCenter);
    livePreview_->setScaledContents(false);
    liveLayout->addWidget(liveSceneLabel_);
    liveLayout->addWidget(liveBadge_);
    liveLayout->addWidget(livePreview_);
    root->addWidget(liveBox);

    auto *aiBox = new QGroupBox(QStringLiteral("INTELIGÊNCIA — GEMINI"));
    auto *aiLayout = new QVBoxLayout(aiBox);
    aiLayout->setContentsMargins(7, 5, 7, 6);

    auto *modelRow = new QHBoxLayout();
    auto *modelLabel = new QLabel(QStringLiteral("Modelo"));
    modelEdit_ = new QLineEdit(settings.value(QStringLiteral("model"), QStringLiteral("gemini-3.8-flash")).toString());
    modelEdit_->setToolTip(QStringLiteral("Modelo Gemini usado para visão + áudio."));
    modelRow->addWidget(modelLabel);
    modelRow->addWidget(modelEdit_, 1);
    aiLayout->addLayout(modelRow);

    auto *keyRow = new QHBoxLayout();
    auto *keyLabel = new QLabel(QStringLiteral("API Key"));
    apiKeyEdit_ = new QLineEdit(settings.value(QStringLiteral("api_key")).toString());
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    apiKeyEdit_->setPlaceholderText(QStringLiteral("Cole sua chave do Google AI Studio"));
    keyRow->addWidget(keyLabel);
    keyRow->addWidget(apiKeyEdit_, 1);
    aiLayout->addLayout(keyRow);

    auto *aiButtons = new QHBoxLayout();
    testAIButton_ = new QPushButton(QStringLiteral("TESTAR IA"));
    analyzeButton_ = new QPushButton(QStringLiteral("ANALISAR AGORA"));
    aiButtons->addWidget(testAIButton_, 1);
    aiButtons->addWidget(analyzeButton_, 1);
    aiLayout->addLayout(aiButtons);

    aiStatusLabel_ = new QLabel(QStringLiteral("● IA: aguardando configuração"));
    aiStatusLabel_->setObjectName(QStringLiteral("aiStatus"));
    aiStatusLabel_->setWordWrap(true);
    aiLayout->addWidget(aiStatusLabel_);
    apiStateLabel_ = new QLabel(apiKeyEdit_->text().trimmed().isEmpty()
                                    ? QStringLiteral("API: não configurada")
                                    : QStringLiteral("API: chave salva"));
    apiStateLabel_->setObjectName(QStringLiteral("apiState"));
    aiLayout->addWidget(apiStateLabel_);
    root->addWidget(aiBox);

    auto *engineBox = new QGroupBox(QStringLiteral("MOTOR DE DIREÇÃO"));
    auto *engineLayout = new QVBoxLayout(engineBox);
    engineLayout->setContentsMargins(7, 5, 7, 6);

    auto *engineRadios = new QHBoxLayout();
    iaRadio_ = new QRadioButton(QStringLiteral("IA — Gemini"));
    predefRadio_ = new QRadioButton(QStringLiteral("PREDEFINIDO — sem IA"));
    iaRadio_->setChecked(settings.value(QStringLiteral("engine"), QStringLiteral("ia")).toString() != QStringLiteral("predefinido"));
    predefRadio_->setChecked(!iaRadio_->isChecked());
    engineGroup_ = new QButtonGroup(this);
    engineGroup_->addButton(iaRadio_, static_cast<int>(Engine::IA));
    engineGroup_->addButton(predefRadio_, static_cast<int>(Engine::Predefinido));
    engineRadios->addWidget(iaRadio_);
    engineRadios->addWidget(predefRadio_);
    engineLayout->addLayout(engineRadios);

    auto *engineHint = new QLabel(QStringLiteral("Predefinido não alterna cenas por conta própria. Ele cruza cena atual + tempo + áudio + movimento + intervalo de corte e só gera uma ação quando uma regra configurada realmente coincide."));
    engineHint->setWordWrap(true);
    engineHint->setObjectName(QStringLiteral("engineHint"));
    engineLayout->addWidget(engineHint);

    rulesTable_ = new QTableWidget(0, 3);
    rulesTable_->setHorizontalHeaderLabels(QStringList() << QStringLiteral("Condição: cena atual")
                                                        << QStringLiteral("Ação: ir para")
                                                        << QStringLiteral("Após (s)"));
    rulesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rulesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    rulesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rulesTable_->verticalHeader()->setVisible(false);
    rulesTable_->horizontalHeader()->setStretchLastSection(false);
    rulesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    rulesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    rulesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    rulesTable_->setMinimumHeight(145);
    engineLayout->addWidget(rulesTable_);

    auto *ruleButtons = new QHBoxLayout();
    addRuleButton_ = new QPushButton(QStringLiteral("+ REGRA"));
    removeRuleButton_ = new QPushButton(QStringLiteral("− REMOVER"));
    defaultRulesButton_ = new QPushButton(QStringLiteral("LIMPAR REGRAS"));
    ruleButtons->addWidget(addRuleButton_);
    ruleButtons->addWidget(removeRuleButton_);
    ruleButtons->addWidget(defaultRulesButton_);
    engineLayout->addLayout(ruleButtons);
    root->addWidget(engineBox);

    auto *suggestionBox = new QGroupBox(QStringLiteral("SUGESTÃO DE CORTE"));
    auto *suggestionLayout = new QVBoxLayout(suggestionBox);
    suggestionLayout->setContentsMargins(7, 5, 7, 6);

    auto *suggestionTop = new QHBoxLayout();
    suggestionPreview_ = new QLabel(QStringLiteral("SEM\nPRÉVIA"));
    suggestionPreview_->setObjectName(QStringLiteral("suggestionPreview"));
    suggestionPreview_->setFixedSize(128, 72);
    suggestionPreview_->setAlignment(Qt::AlignCenter);
    suggestionSceneLabel_ = new QLabel(QStringLiteral("Aguardando análise"));
    suggestionSceneLabel_->setObjectName(QStringLiteral("suggestionScene"));
    suggestionSceneLabel_->setWordWrap(true);
    suggestionTop->addWidget(suggestionPreview_);
    suggestionTop->addWidget(suggestionSceneLabel_, 1);
    suggestionLayout->addLayout(suggestionTop);

    confidenceLabel_ = new QLabel(QStringLiteral("Confiança: —"));
    confidenceLabel_->setObjectName(QStringLiteral("confidence"));
    reasonsLabel_ = new QLabel(QStringLiteral("O Diretor analisará as imagens das cenas candidatas e o áudio do programa."));
    reasonsLabel_->setWordWrap(true);
    reasonsLabel_->setObjectName(QStringLiteral("reasons"));
    suggestionLayout->addWidget(confidenceLabel_);
    suggestionLayout->addWidget(reasonsLabel_);

    auto *buttons = new QHBoxLayout();
    cutButton_ = new QPushButton(QStringLiteral("✓  CORTAR"));
    ignoreButton_ = new QPushButton(QStringLiteral("↶  IGNORAR"));
    cutButton_->setObjectName(QStringLiteral("cutButton"));
    ignoreButton_->setObjectName(QStringLiteral("ignoreButton"));
    buttons->addWidget(cutButton_, 1);
    buttons->addWidget(ignoreButton_, 1);
    suggestionLayout->addLayout(buttons);
    root->addWidget(suggestionBox);

    auto *monitorBox = new QGroupBox(QStringLiteral("MONITORAMENTO AO VIVO"));
    auto *monitorLayout = new QVBoxLayout(monitorBox);
    monitorLayout->setContentsMargins(7, 5, 7, 6);
    audioStatusLabel_ = new QLabel(QStringLiteral("Microfone/Áudio: 0% • parado"));
    motionStatusLabel_ = new QLabel(QStringLiteral("Movimento da imagem: 0% • parado"));
    monitorLayout->addWidget(audioStatusLabel_);
    monitorLayout->addWidget(motionStatusLabel_);
    root->addWidget(monitorBox);

    auto *analysisBox = new QGroupBox(QStringLiteral("PRÓXIMA ANÁLISE"));
    auto *analysisLayout = new QVBoxLayout(analysisBox);
    analysisLayout->setContentsMargins(7, 5, 7, 6);
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
    modeLayout->setContentsMargins(7, 5, 7, 6);
    manualRadio_ = new QRadioButton(QStringLiteral("Manual     • apenas informações e alertas"));
    assistidoRadio_ = new QRadioButton(QStringLiteral("Assistido  • sugere e aguarda sua decisão"));
    automaticoRadio_ = new QRadioButton(QStringLiteral("Automático • executa cortes com segurança"));
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
    auto *summaryLayout = new QGridLayout(summaryBox);
    summaryLayout->setContentsMargins(7, 5, 7, 6);
    timeLabel_ = new QLabel(QStringLiteral("Tempo de transmissão\n00:00:00"));
    cutsLabel_ = new QLabel(QStringLiteral("Cortes realizados\n0"));
    mostUsedLabel_ = new QLabel(QStringLiteral("Cena mais usada\n—"));
    summaryLayout->addWidget(timeLabel_, 0, 0);
    summaryLayout->addWidget(cutsLabel_, 0, 1);
    summaryLayout->addWidget(mostUsedLabel_, 0, 2);
    root->addWidget(summaryBox);

    analysisTimer_ = new QTimer(this);
    progressTimer_ = new QTimer(this);
    captureTimer_ = new QTimer(this);
    analysisTimer_->setInterval(kAnalysisIntervalMs);
    progressTimer_->setInterval(100);
    captureTimer_->setInterval(kCandidateCaptureIntervalMs);

    connect(directorToggleButton_, &QPushButton::clicked, this, &DiretorDock::toggleDirector);
    connect(cutButton_, &QPushButton::clicked, this, &DiretorDock::cutSuggestion);
    connect(ignoreButton_, &QPushButton::clicked, this, &DiretorDock::ignoreSuggestion);
    connect(analyzeButton_, &QPushButton::clicked, this, &DiretorDock::analyzeNow);
    connect(testAIButton_, &QPushButton::clicked, this, &DiretorDock::testAIConnection);
    connect(modeGroup_, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int) { modeChanged(); });
    connect(engineGroup_, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int) { engineChanged(); });
    connect(addRuleButton_, &QPushButton::clicked, this, &DiretorDock::addPredefinedRule);
    connect(removeRuleButton_, &QPushButton::clicked, this, &DiretorDock::removePredefinedRule);
    connect(defaultRulesButton_, &QPushButton::clicked, this, [this]() {
        rulesTable_->setRowCount(0);
        triggeredRuleKeys_.clear();
        savePredefinedRules();
        setAIStatus(QStringLiteral("● SEM IA: regras limpas — nenhuma ação automática será inventada"), true);
        updateUi();
    });
    connect(rulesTable_, &QTableWidget::cellChanged, this, &DiretorDock::predefinedRuleCellChanged);
    connect(apiKeyEdit_, &QLineEdit::editingFinished, this, &DiretorDock::saveApiSettings);
    connect(modelEdit_, &QLineEdit::editingFinished, this, &DiretorDock::saveApiSettings);
    connect(analysisTimer_, &QTimer::timeout, this, &DiretorDock::analyze);
    connect(progressTimer_, &QTimer::timeout, this, &DiretorDock::updateProgress);
    connect(captureTimer_, &QTimer::timeout, this, &DiretorDock::captureCandidates);

    setStyleSheet(QStringLiteral(R"(
        QWidget#DiretorDeCulto { background:#101820; color:#edf3f6; font-family:"Segoe UI"; font-size:9pt; }
        QLabel#mainTitle { font-size:15pt; font-weight:900; color:#ffffff; }
        QLabel#activeStatus { color:#27e68a; font-weight:900; }
        QGroupBox { border:1px solid #244052; border-radius:8px; margin-top:7px; padding-top:7px; background:#121e28; font-weight:800; }
        QGroupBox::title { subcontrol-origin:margin; left:9px; padding:0 5px; color:#d9e5eb; }
        QLabel#liveScene { font-size:12pt; font-weight:900; color:#ffffff; padding:3px 4px; background:#182732; border-radius:5px; }
        QLabel#liveBadge { color:#ff4141; font-weight:900; padding-left:4px; }
        QLabel#livePreview { background:#081018; border:1px solid #355263; border-radius:6px; color:#78909c; font-weight:800; }
        QLabel#aiStatus { color:#ffc04a; font-weight:800; }
        QLabel#apiState { color:#8fa8b7; font-size:8pt; }
        QLabel#engineHint { color:#8fa8b7; font-size:8pt; }
        QTableWidget { background:#0b141b; border:1px solid #38515f; gridline-color:#233844; color:#edf3f6; }
        QTableWidget::item:selected { background:#24485b; }
        QHeaderView::section { background:#182732; color:#d9e5eb; border:0; padding:4px; font-weight:800; }
        QComboBox, QSpinBox { min-height:24px; background:#0b141b; border:1px solid #38515f; border-radius:4px; color:#ffffff; padding:0 5px; }
        QLabel#suggestionScene { color:#ffffff; font-size:12pt; font-weight:900; }
        QLabel#suggestionPreview { background:#081018; border:1px solid #355263; border-radius:6px; color:#78909c; font-weight:800; }
        QLabel#confidence { color:#55f0a0; font-weight:900; }
        QLabel#reasons { color:#d2dde2; }
        QPushButton { min-height:29px; border-radius:5px; border:1px solid #355263; background:#1b2a35; color:#ffffff; font-weight:800; padding:0 8px; }
        QPushButton:hover { background:#263b49; }
        QPushButton#directorToggle { background:#0c6c4b; border-color:#25d995; min-height:36px; }
        QPushButton#directorToggle:checked { background:#7a2525; border-color:#ff5454; }
        QPushButton#cutButton { background:#0a9b67; border-color:#21d99a; }
        QPushButton#cutButton:disabled { background:#17342b; color:#648477; border-color:#254b3d; }
        QPushButton#ignoreButton { background:#263746; }
        QPushButton:disabled { color:#6f7e86; }
        QLineEdit { min-height:25px; background:#0b141b; border:1px solid #38515f; border-radius:5px; color:#ffffff; padding:0 7px; }
        QProgressBar { height:7px; border:0; border-radius:4px; background:#20313c; }
        QProgressBar::chunk { background:#25e19a; border-radius:4px; }
        QRadioButton { spacing:6px; padding:2px; }
        QRadioButton::indicator { width:13px; height:13px; }
    )"));

    buildCandidateGrid();
    loadPredefinedRules();
    refreshFromObs();
    engineChanged();
}

DiretorDock::~DiretorDock()
{
    stopLoop();
    if (networkThread_.joinable())
        networkThread_.join();
    networkBusy_.store(false);
}

QString DiretorDock::apiKey() const
{
    return apiKeyEdit_ ? apiKeyEdit_->text().trimmed() : QString();
}

QString DiretorDock::modelName() const
{
    const QString model = modelEdit_ ? modelEdit_->text().trimmed() : QString();
    return model.isEmpty() ? QStringLiteral("gemini-3.8-flash") : model;
}

void DiretorDock::saveApiSettings()
{
    QSettings settings(QStringLiteral("DiretorDeCulto"), QStringLiteral("OBS"));
    settings.setValue(QStringLiteral("api_key"), apiKey());
    settings.setValue(QStringLiteral("model"), modelName());
    apiStateLabel_->setText(apiKey().isEmpty() ? QStringLiteral("API: não configurada")
                                                : QStringLiteral("API: chave salva"));
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

QStringList DiretorDock::candidateSceneNames() const
{
    const QStringList preferred = {
        QStringLiteral("1 - PASTOR"), QStringLiteral("PASTOR EDIT"),
        QStringLiteral("2 - SOLO"), QStringLiteral("SOLO EDIT"),
        QStringLiteral("3 - REGENTE"), QStringLiteral("4 - ORQ. DIR"),
        QStringLiteral("5 - ORQ. ESQ"), QStringLiteral("6 - VIOLÃO"),
        QStringLiteral("CAMERA 1"), QStringLiteral("CAMERA 2"),
        QStringLiteral("CAM EDIT 2")
    };
    const QStringList all = allSceneNames();
    QStringList result;
    for (const QString &name : preferred) {
        if (all.contains(name))
            result << name;
    }
    return result;
}

void DiretorDock::buildCandidateGrid()
{
    // The visual candidate grid is represented by the suggestion preview in V5.
    // The plugin still captures all configured candidate scenes for Gemini.
    candidateNames_ = candidateSceneNames().toVector();
}

void DiretorDock::updateCandidateGrid()
{
    Q_UNUSED(candidateThumbs_);
}

bool DiretorDock::renderSourceToImage(obs_source_t *source, QImage &out) const
{
    if (!source)
        return false;

    QMutexLocker graphicsLocker(&captureMutex_);
    obs_enter_graphics();

    bool ok = false;
    gs_texrender_t *render = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    gs_stagesurf_t *stage = gs_stagesurface_create(kFrameWidth, kFrameHeight, GS_BGRA);

    if (render && stage && gs_texrender_begin(render, kFrameWidth, kFrameHeight)) {
        struct vec4 clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        gs_clear(GS_CLEAR_COLOR, &clearColor, 0.0f, 0);
        gs_ortho(0.0f, static_cast<float>(kFrameWidth), 0.0f,
                 static_cast<float>(kFrameHeight), -100.0f, 100.0f);
        obs_source_video_render(source);
        gs_texrender_end(render);

        gs_stage_texture(stage, gs_texrender_get_texture(render));
        gs_flush();

        uint8_t *data = nullptr;
        uint32_t linesize = 0;
        if (gs_stagesurface_map(stage, &data, &linesize) && data) {
            QImage mapped(data, kFrameWidth, kFrameHeight, static_cast<int>(linesize), QImage::Format_ARGB32);
            out = mapped.copy().mirrored(false, true);
            gs_stagesurface_unmap(stage);
            ok = !out.isNull();
        }
    }

    if (stage)
        gs_stagesurface_destroy(stage);
    if (render)
        gs_texrender_destroy(render);
    obs_leave_graphics();
    return ok;
}

void DiretorDock::captureSceneImages()
{
    if (!directorEnabled_ || !streamingActive_)
        return;

    const QStringList candidates = candidateSceneNames();
    if (candidates.isEmpty()) {
        setAIStatus(QStringLiteral("● IA: nenhuma cena candidata configurada"), false);
        return;
    }

    QHash<QString, QImage> captured;
    for (const QString &sceneName : candidates) {
        obs_source_t *source = obs_get_source_by_name(sceneName.toUtf8().constData());
        if (!source)
            continue;
        QImage image;
        if (renderSourceToImage(source, image))
            captured.insert(sceneName, image);
        obs_source_release(source);
    }

    candidateFrames_ = captured;

    const QString target = suggestion_.scene;
    if (!target.isEmpty() && candidateFrames_.contains(target)) {
        const QImage image = candidateFrames_.value(target);
        suggestionPreview_->setPixmap(QPixmap::fromImage(image).scaled(suggestionPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        suggestionPreview_->setText(QString());
    } else if (candidateFrames_.contains(currentSceneName())) {
        const QImage image = candidateFrames_.value(currentSceneName());
        suggestionPreview_->setPixmap(QPixmap::fromImage(image).scaled(suggestionPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        suggestionPreview_->setText(QString());
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
    if (!captureTimer_->isActive())
        captureTimer_->start();
    // O monitoramento local é usado pelos dois motores. No modo PREDEFINIDO
    // nenhum frame/áudio é enviado para a internet; ele serve apenas para
    // validar condições objetivas da regra.
    startMediaAnalysis();
    captureSceneImages();
    if (engine_ == Engine::IA) {
        if (apiKey().isEmpty())
            setAIStatus(QStringLiteral("● IA: informe a API Key para começar"), false);
        else
            setAIStatus(QStringLiteral("● IA: pronta • %1").arg(modelName()), true);
    } else {
        setAIStatus(QStringLiteral("● SEM IA: regras + condições locais ativas"), true);
    }
}

void DiretorDock::stopLoop()
{
    if (analysisTimer_) analysisTimer_->stop();
    if (progressTimer_) progressTimer_->stop();
    if (captureTimer_) captureTimer_->stop();
    stopMediaAnalysis();
    if (progressBar_) progressBar_->setValue(0);
    if (nextAnalysisLabel_)
        nextAnalysisLabel_->setText(directorEnabled_ ? QStringLiteral("Aguardando transmissão") : QStringLiteral("Diretor desligado"));
    aiBusy_ = false;
    requestKind_ = RequestKind::None;
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
        aiCooldownSeconds_ = 0;
        secondsInCurrentScene_ = 0;
        observedScene_ = currentSceneName();
        lastCutScene_.clear();
        recentScenes_.clear();
        if (!observedScene_.isEmpty())
            recentScenes_.append(observedScene_);
        triggeredRuleKeys_.clear();
        suggestion_ = Suggestion{};
        lastIgnoredScene_.clear();
        candidateFrames_.clear();
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
        directorToggleButton_->setText(QStringLiteral("■  DESLIGAR DIRETOR DE CULTO"));
        setStatus(streamingActive_ ? QStringLiteral("● DIRETOR ATIVO") : QStringLiteral("● AGUARDANDO STREAM"), true);
        if (streamingActive_)
            startLoop();
    } else {
        directorToggleButton_->setText(QStringLiteral("▶  LIGAR DIRETOR DE CULTO"));
        setStatus(QStringLiteral("● DESLIGADO"), false);
        stopLoop();
        suggestion_ = Suggestion{};
        candidateFrames_.clear();
        updateUi();
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
    audioRms_.store(0.0f, std::memory_order_relaxed);
    motionScore_.store(0.0f, std::memory_order_relaxed);
    {
        QMutexLocker lock(&frameMutex_);
        latestFrame_ = QImage();
        previousLuma_.clear();
    }
    {
        QMutexLocker lock(&audioMutex_);
        audioBuffer_.clear();
    }
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
    const uint32_t sampleCount = data->frames * static_cast<uint32_t>(kAudioChannels);
    double sum = 0.0;
    {
        QMutexLocker lock(&audioMutex_);
        audioBuffer_.reserve(audioBuffer_.size() + sampleCount);
        for (uint32_t i = 0; i < sampleCount; ++i) {
            const float s = std::clamp(samples[i], -1.0f, 1.0f);
            sum += static_cast<double>(s) * static_cast<double>(s);
            audioBuffer_.push_back(s);
        }
        const size_t maxSamples = kAudioKeepSeconds * kAudioSampleRate * kAudioChannels;
        if (audioBuffer_.size() > maxSamples)
            audioBuffer_.erase(audioBuffer_.begin(), audioBuffer_.begin() + static_cast<ptrdiff_t>(audioBuffer_.size() - maxSamples));
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
    if (counter - lastCopiedFrame_ < 10)
        return;
    QImage image(reinterpret_cast<const uchar *>(frame->data[0]), width, height, stride, QImage::Format_ARGB32);
    if (image.isNull())
        return;
    QMutexLocker lock(&frameMutex_);
    latestFrame_ = image.copy();
    lastCopiedFrame_ = counter;
}

QByteArray DiretorDock::buildWavAudio() const
{
    QVector<float> samples;
    {
        QMutexLocker lock(&audioMutex_);
        samples.reserve(static_cast<int>(audioBuffer_.size()));
        for (float value : audioBuffer_)
            samples.append(value);
    }
    if (samples.isEmpty())
        return QByteArray();

    QByteArray pcm;
    pcm.reserve(samples.size() * 2);
    for (float sample : samples) {
        const int value = std::clamp(static_cast<int>(std::lrint(sample * 32767.0f)), -32768, 32767);
        appendLE16(pcm, static_cast<quint16>(static_cast<qint16>(value)));
    }

    QByteArray wav;
    wav.reserve(44 + pcm.size());
    wav.append("RIFF", 4);
    appendLE32(wav, 36 + static_cast<quint32>(pcm.size()));
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    appendLE32(wav, 16);
    appendLE16(wav, 1);
    appendLE16(wav, static_cast<quint16>(kAudioChannels));
    appendLE32(wav, static_cast<quint32>(kAudioSampleRate));
    appendLE32(wav, static_cast<quint32>(kAudioSampleRate * kAudioChannels * 2));
    appendLE16(wav, static_cast<quint16>(kAudioChannels * 2));
    appendLE16(wav, 16);
    wav.append("data", 4);
    appendLE32(wav, static_cast<quint32>(pcm.size()));
    wav.append(pcm);
    return wav;
}

QString DiretorDock::buildAIPrompt() const
{
    const QString current = currentSceneName();
    const QStringList scenes = allSceneNames();
    const QStringList candidates = candidateSceneNames();
    const float rms = audioRms_.load(std::memory_order_relaxed);
    const float motion = motionScore_.load(std::memory_order_relaxed);

    QString prompt;
    prompt += QStringLiteral("Você é o diretor técnico de uma transmissão ao vivo de culto cristão.\n");
    prompt += QStringLiteral("Você NÃO deve alternar cenas por rotina, por tempo isolado ou por preferência estética. Decida somente quando houver evidência suficiente e coerente.\n");
    prompt += QStringLiteral("A decisão precisa considerar simultaneamente: cena atual, tempo nela, histórico recente de cenas, áudio, movimento, imagens das cenas candidatas, contexto visual e segurança contra cortes excessivos.\n");
    prompt += QStringLiteral("Se uma única evidência não for suficiente, responda HOLD. Se duas ou mais evidências independentes apontarem para o mesmo alvo, a confiança pode subir.\n\n");
    prompt += QStringLiteral("CENA ATUAL NO AR: ") + current + QStringLiteral("\n");
    prompt += QStringLiteral("TEMPO NA CENA ATUAL: ") + QString::number(secondsInCurrentScene_) + QStringLiteral(" s\n");
    prompt += QStringLiteral("TEMPO DESDE O ÚLTIMO CORTE: ") + QString::number(secondsSinceCut_) + QStringLiteral(" s\n");
    prompt += QStringLiteral("ÁUDIO RMS: ") + QString::number(rms, 'f', 3) + QStringLiteral("\n");
    prompt += QStringLiteral("MOVIMENTO DO PROGRAMA: ") + QString::number(motion, 'f', 3) + QStringLiteral("\n");
    prompt += QStringLiteral("HISTÓRICO RECENTE: ") + QStringList::fromVector(recentScenes_).join(QStringLiteral(" -> ")) + QStringLiteral("\n\n");
    prompt += QStringLiteral("CENAS CANDIDATAS RENDERIZADAS: \n- ") + candidates.join(QStringLiteral("\n- ")) + QStringLiteral("\n\n");
    prompt += QStringLiteral("CENAS DISPONÍVEIS NO OBS: \n- ") + scenes.join(QStringLiteral("\n- ")) + QStringLiteral("\n\n");
    prompt += QStringLiteral("Nas imagens, procure evidências concretas: pastor efetivamente falando; solista efetivamente cantando; regente efetivamente conduzindo; coral cantando; músicos tocando; violão sendo tocado; enquadramento que realmente mostra a ação; mudança real de contexto. Não presuma uma ação apenas porque o nome da cena sugere isso.\n");
    prompt += QStringLiteral("No áudio, procure evidência de fala, canto, música, silêncio ou mudança clara de atividade.\n");
    prompt += QStringLiteral("Considere também se o corte acabou de acontecer, se a cena atual continua adequada e se o alvo foi ignorado anteriormente.\n");
    prompt += QStringLiteral("Nunca escolha uma cena inexistente. Se houver dúvida, use HOLD e confidence 0.\n");
    prompt += QStringLiteral("Para AUTOMÁTICO, confidence >= 85 somente quando houver forte concordância entre múltiplos sinais independentes.\n");
    prompt += QStringLiteral("Retorne somente o JSON solicitado pela API.\n");
    return prompt;
}

QJsonObject DiretorDock::buildGeminiBody(bool includeMedia) const
{
    QJsonObject body;
    QJsonArray parts;
    parts.append(QJsonObject{{QStringLiteral("text"), buildAIPrompt()}});

    if (includeMedia) {
        const QStringList candidates = candidateSceneNames();
        for (const QString &sceneName : candidates) {
            if (!candidateFrames_.contains(sceneName))
                continue;
            QByteArray imageBytes;
            QBuffer buffer(&imageBytes);
            buffer.open(QIODevice::WriteOnly);
            candidateFrames_.value(sceneName).save(&buffer, "JPG", 70);
            buffer.close();
            // Gemini Part usa oneof: texto e mídia NÃO podem ficar no mesmo objeto Part.
            QJsonObject imageLabelPart;
            imageLabelPart.insert(QStringLiteral("text"), QStringLiteral("IMAGEM DA CENA: ") + sceneName);
            parts.append(imageLabelPart);

            QJsonObject imageBlob;
            imageBlob.insert(QStringLiteral("mime_type"), QStringLiteral("image/jpeg"));
            imageBlob.insert(QStringLiteral("data"), QString::fromLatin1(imageBytes.toBase64()));
            QJsonObject imagePart;
            imagePart.insert(QStringLiteral("inline_data"), imageBlob);
            parts.append(imagePart);
        }

        const QByteArray audio = buildWavAudio();
        if (!audio.isEmpty()) {
            QJsonObject audioLabelPart;
            audioLabelPart.insert(QStringLiteral("text"), QStringLiteral("ÁUDIO DO PROGRAMA — últimos segundos"));
            parts.append(audioLabelPart);

            QJsonObject audioBlob;
            audioBlob.insert(QStringLiteral("mime_type"), QStringLiteral("audio/wav"));
            audioBlob.insert(QStringLiteral("data"), QString::fromLatin1(audio.toBase64()));
            QJsonObject audioPart;
            audioPart.insert(QStringLiteral("inline_data"), audioBlob);
            parts.append(audioPart);
        }
    }

    body.insert(QStringLiteral("contents"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                                      {QStringLiteral("parts"), parts}}});

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("OBJECT"));
    QJsonObject props;
    props.insert(QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("STRING")},
                                                          {QStringLiteral("enum"), QJsonArray{QStringLiteral("CUT"), QStringLiteral("HOLD")}}});
    props.insert(QStringLiteral("scene"), QJsonObject{{QStringLiteral("type"), QStringLiteral("STRING")}});
    props.insert(QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("INTEGER")}});
    props.insert(QStringLiteral("role"), QJsonObject{{QStringLiteral("type"), QStringLiteral("STRING")}});
    props.insert(QStringLiteral("reasons"), QJsonObject{{QStringLiteral("type"), QStringLiteral("ARRAY")},
                                                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("STRING")}}}});
    props.insert(QStringLiteral("evidence_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("INTEGER")}});
    schema.insert(QStringLiteral("properties"), props);
    schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("action"), QStringLiteral("scene"), QStringLiteral("confidence"), QStringLiteral("reasons"), QStringLiteral("evidence_count")});

    QJsonObject generation;
    generation.insert(QStringLiteral("maxOutputTokens"), 256);
    generation.insert(QStringLiteral("responseMimeType"), QStringLiteral("application/json"));
    generation.insert(QStringLiteral("responseSchema"), schema);
    body.insert(QStringLiteral("generationConfig"), generation);
    return body;
}

void DiretorDock::requestGeminiTest()
{
    if (apiKey().isEmpty()) {
        setAIStatus(QStringLiteral("● IA: informe a API Key"), false);
        return;
    }
    if (networkBusy_.load()) return;
    QJsonObject body;
    QJsonObject testTextPart;
    testTextPart.insert(QStringLiteral("text"), QStringLiteral("Responda somente OK para confirmar que a API Gemini está acessível."));
    QJsonArray testParts;
    testParts.append(testTextPart);
    QJsonObject testContent;
    testContent.insert(QStringLiteral("parts"), testParts);
    QJsonArray testContents;
    testContents.append(testContent);
    body.insert(QStringLiteral("contents"), testContents);
    QJsonObject generation;
    generation.insert(QStringLiteral("maxOutputTokens"), 8);
    body.insert(QStringLiteral("generationConfig"), generation);
    const QString url = QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent").arg(modelName());
    startGeminiHttpRequest(RequestKind::Test, url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    setAIStatus(QStringLiteral("● IA: testando Gemini via HTTPS do Windows..."), false);
}

void DiretorDock::requestGemini()
{
    if (!directorEnabled_ || !streamingActive_ || aiBusy_ || networkBusy_.load()) return;
    if (apiKey().isEmpty()) {
        setAIStatus(QStringLiteral("● IA: API Key não configurada"), false);
        return;
    }
    if (candidateFrames_.isEmpty()) {
        captureSceneImages();
        if (candidateFrames_.isEmpty()) {
            setAIStatus(QStringLiteral("● IA: aguardando imagens das cenas"), false);
            return;
        }
    }
    const QJsonObject body = buildGeminiBody(true);
    const QString url = QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent").arg(modelName());
    startGeminiHttpRequest(RequestKind::Analyze, url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    aiBusy_ = true;
    secondsSinceAI_ = 0;
    setAIStatus(QStringLiteral("● IA: analisando câmeras + áudio..."), false);
}

void DiretorDock::startGeminiHttpRequest(RequestKind kind, const QString &url, const QByteArray &body)
{
#ifndef _WIN32
    setAIStatus(QStringLiteral("● IA: transporte HTTPS do Windows não disponível"), false);
#else
    if (networkBusy_.exchange(true)) return;
    if (networkThread_.joinable()) networkThread_.join();
    const QByteArray key = apiKey().toUtf8();
    requestKind_ = kind;
    networkThread_ = std::thread([this, kind, url, body, key]() {
        const WinHttpResult result = postGeminiWinHttp(url, body, key);
        QMetaObject::invokeMethod(this, [this, kind, result]() {
            handleAIResult(kind, result.status, result.error, result.body);
        }, Qt::QueuedConnection);
    });
#endif
}

void DiretorDock::analyzeNow()
{
    if (!directorEnabled_ || !streamingActive_) return;
    saveApiSettings();
    if (engine_ == Engine::IA) {
        captureSceneImages();
        requestGemini();
    } else {
        evaluatePredefinedRules();
        updateUi();
    }
}

void DiretorDock::testAIConnection()
{
    if (engine_ != Engine::IA) {
        setAIStatus(QStringLiteral("● SEM IA: teste de Gemini indisponível neste motor"), true);
        return;
    }
    saveApiSettings();
    requestGeminiTest();
}

void DiretorDock::handleAIResult(RequestKind kind, int httpStatus, const QString &errorText, const QByteArray &raw)
{
    networkBusy_.store(false);
    aiBusy_ = false;
    requestKind_ = RequestKind::None;
    const QString responseText = QString::fromUtf8(raw).left(700).simplified();
    if (!errorText.isEmpty()) {
        QString diagnostic = QStringLiteral("● IA: erro HTTPS Gemini: %1").arg(errorText);
        if (httpStatus > 0) diagnostic += QStringLiteral(" | HTTP %1").arg(httpStatus);
        if (!responseText.isEmpty()) diagnostic += QStringLiteral(" | ") + responseText;
        suggestion_.scene = currentSceneName();
        suggestion_.confidence = 0;
        suggestion_.evidenceCount = 0;
        suggestion_.reasons = QStringList() << diagnostic;
        suggestion_.source = QStringLiteral("Gemini • erro");
        setAIStatus(diagnostic, false);
        updateUi();
        return;
    }
    if (httpStatus >= 400 || httpStatus == 0) {
        QString diagnostic = QStringLiteral("● IA: Gemini HTTP %1").arg(httpStatus);
        if (!responseText.isEmpty()) diagnostic += QStringLiteral(" | ") + responseText;
        if (httpStatus == 429) {
            aiCooldownSeconds_ = kAIErrorCooldownSeconds;
            diagnostic += QStringLiteral(" | nova tentativa em %1s").arg(kAIErrorCooldownSeconds);
        }
        suggestion_.scene = currentSceneName();
        suggestion_.confidence = 0;
        suggestion_.evidenceCount = 0;
        suggestion_.reasons = QStringList() << diagnostic;
        suggestion_.source = QStringLiteral("Gemini • sem decisão");
        setAIStatus(diagnostic, false);
        updateUi();
        updateUi();
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (!doc.isObject()) {
        setAIStatus(QStringLiteral("● IA: resposta inválida do Gemini"), false);
        return;
    }
    if (kind == RequestKind::Test) {
        aiOnline_ = true;
        setAIStatus(QStringLiteral("● IA: Gemini conectado — HTTP %1").arg(httpStatus), true);
        return;
    }
    const QJsonArray candidates = doc.object().value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty()) {
        setAIStatus(QStringLiteral("● IA: Gemini não retornou candidato"), false);
        return;
    }
    const QJsonArray parts = candidates.first().toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("parts")).toArray();
    QString text;
    for (const QJsonValue &part : parts) text += part.toObject().value(QStringLiteral("text")).toString();
    QJsonParseError decisionError{};
    const QJsonDocument decisionDoc = QJsonDocument::fromJson(text.toUtf8(), &decisionError);
    if (!decisionDoc.isObject()) {
        setAIStatus(QStringLiteral("● IA: JSON de decisão inválido"), false);
        return;
    }
    aiOnline_ = true;
    processGeminiDecision(decisionDoc.object());
}

void DiretorDock::processGeminiDecision(const QJsonObject &decision)
{
    const QString target = jsonString(decision, "scene");
    const QString action = jsonString(decision, "action").toUpper();
    const int confidence = std::clamp(decision.value(QStringLiteral("confidence")).toInt(0), 0, 100);
    const int evidenceCount = std::clamp(decision.value(QStringLiteral("evidence_count")).toInt(0), 0, 10);
    QStringList reasons;
    const QJsonArray arr = decision.value(QStringLiteral("reasons")).toArray();
    for (const QJsonValue &value : arr) {
        const QString reason = value.toString().trimmed();
        if (!reason.isEmpty())
            reasons << reason;
    }

    const QStringList scenes = allSceneNames();
    const QString current = currentSceneName();
    if (action != QStringLiteral("CUT") || target.isEmpty() || !scenes.contains(target) || target == current || target == lastIgnoredScene_) {
        suggestion_.scene = current;
        suggestion_.confidence = 0;
        suggestion_.reasons = reasons.isEmpty()
                                  ? QStringList{QStringLiteral("A IA não encontrou evidência suficiente para trocar a cena.")}
                                  : reasons;
        suggestion_.source = QStringLiteral("Gemini • HOLD");
        suggestion_.evidenceCount = evidenceCount;
        setAIStatus(QStringLiteral("● IA: conectada • mantendo %1").arg(current), true);
        updateUi();
        return;
    }

    suggestion_.scene = target;
    suggestion_.confidence = confidence;
    suggestion_.reasons = reasons.isEmpty() ? QStringList{QStringLiteral("Evidência visual/sonora da IA")} : reasons;
    suggestion_.source = QStringLiteral("Gemini • visão + áudio");
    suggestion_.evidenceCount = evidenceCount;

    const QString role = jsonString(decision, "role");
    if (!role.isEmpty())
        suggestion_.reasons.prepend(QStringLiteral("Contexto: %1").arg(role));

    setAIStatus(QStringLiteral("● IA: conectada • sugestão: %1 (%2%)").arg(target).arg(confidence), true);
    updateUi();

    if (mode_ == Mode::Automatico && confidence >= 85 && secondsSinceCut_ >= kMinimumCutIntervalSeconds)
        applyScene(target);
}

void DiretorDock::refreshFromObs()
{
    updateStreamingState();
    candidateNames_ = candidateSceneNames().toVector();
    updateUi();
}

void DiretorDock::captureCandidates()
{
    if (engine_ == Engine::IA && directorEnabled_ && streamingActive_)
        captureSceneImages();
}

void DiretorDock::analyze()
{
    if (!directorEnabled_ || !streamingActive_)
        return;

    elapsedSeconds_++;
    secondsSinceCut_++;
    secondsSinceAnalysis_++;
    secondsSinceAI_++;
    if (aiCooldownSeconds_ > 0)
        --aiCooldownSeconds_;

    setStatus(mode_ == Mode::Automatico ? QStringLiteral("● AUTOMÁTICO")
                                         : mode_ == Mode::Assistido ? QStringLiteral("● ASSISTIDO")
                                                                    : QStringLiteral("● MANUAL"), true);

    const QString current = currentSceneName();
    if (current != observedScene_) {
        observedScene_ = current;
        secondsInCurrentScene_ = 0;
        triggeredRuleKeys_.clear();
        if (!current.isEmpty()) {
            if (recentScenes_.isEmpty() || recentScenes_.last() != current)
                recentScenes_.append(current);
            while (recentScenes_.size() > kMaxRecentScenes)
                recentScenes_.removeFirst();
        }
    } else {
        secondsInCurrentScene_++;
    }

    if (engine_ == Engine::Predefinido) {
        evaluatePredefinedRules();
    } else if (secondsSinceAI_ >= kAIAnalysisEverySeconds && !aiBusy_ && aiCooldownSeconds_ <= 0) {
        captureSceneImages();
        requestGemini();
    }

    updateUi();
}

void DiretorDock::updateUi()
{
    const QString current = currentSceneName();
    liveSceneLabel_->setText(current.isEmpty() ? QStringLiteral("Nenhuma cena selecionada") : current);

    if (streamingActive_) {
        liveBadge_->setText(QStringLiteral("● NO AR"));
        liveBadge_->show();
    } else {
        liveBadge_->clear();
        liveBadge_->hide();
    }

    // A prévia do programa usa o frame real recebido pelo callback de vídeo.
    // Isso não depende do render off-screen das cenas e permanece visível mesmo
    // quando o mecanismo de IA está desligado.
    QImage liveImage;
    {
        QMutexLocker lock(&frameMutex_);
        liveImage = latestFrame_.copy();
    }
    if (!liveImage.isNull()) {
        livePreview_->setPixmap(QPixmap::fromImage(liveImage).scaled(livePreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        livePreview_->setText(QString());
    } else {
        livePreview_->setPixmap(QPixmap());
        livePreview_->setText(streamingActive_ ? QStringLiteral("AGUARDANDO FRAME DO PROGRAMA") : QStringLiteral("PRÉVIA DO PROGRAMA"));
    }

    if (!directorEnabled_) {
        suggestionSceneLabel_->setText(QStringLiteral("Diretor desligado"));
        confidenceLabel_->setText(QStringLiteral("Confiança: —"));
        reasonsLabel_->setText(QStringLiteral("O Diretor está desligado. Nenhum monitoramento ou chamada de IA está ativo."));
    } else if (!streamingActive_) {
        suggestionSceneLabel_->setText(QStringLiteral("Aguardando transmissão"));
        confidenceLabel_->setText(QStringLiteral("Confiança: —"));
        reasonsLabel_->setText(QStringLiteral("O Diretor permanece em espera até o OBS iniciar a transmissão."));
    } else {
        suggestionSceneLabel_->setText(suggestion_.scene.isEmpty() ? QStringLiteral("Aguardando análise...") : suggestion_.scene);
        confidenceLabel_->setText(suggestion_.confidence > 0
                                       ? QStringLiteral("Confiança: %1%").arg(suggestion_.confidence)
                                       : QStringLiteral("Confiança: —"));
        const QString sourcePrefix = suggestion_.source.isEmpty() ? QString() : suggestion_.source + QStringLiteral("\n");
        reasonsLabel_->setText(sourcePrefix + joinReasons(suggestion_.reasons));
    }

    const bool validSuggestion = directorEnabled_ && streamingActive_ &&
                                 !suggestion_.scene.isEmpty() && suggestion_.scene != current &&
                                 suggestion_.confidence > 0;
    cutButton_->setEnabled(validSuggestion && secondsSinceCut_ >= kMinimumCutIntervalSeconds);
    ignoreButton_->setEnabled(validSuggestion);

    const float rms = audioRms_.load(std::memory_order_relaxed);
    const float motion = motionScore_.load(std::memory_order_relaxed);
    const int audioPct = std::clamp(static_cast<int>(rms * 100.0f * 2.5f), 0, 100);
    const int motionPct = std::clamp(static_cast<int>(motion * 100.0f), 0, 100);
    if (streamingActive_ && directorEnabled_) {
        audioStatusLabel_->setText(QStringLiteral("Áudio principal: %1% • %2")
                                       .arg(audioPct)
                                       .arg(audioPct > 6 ? QStringLiteral("sinal ativo") : QStringLiteral("baixo/silêncio")));
        motionStatusLabel_->setText(QStringLiteral("Movimento do programa: %1% • %2")
                                        .arg(motionPct)
                                        .arg(motionPct > 12 ? QStringLiteral("mudança") : QStringLiteral("estável")));
    } else {
        audioStatusLabel_->setText(QStringLiteral("Áudio principal: 0% • parado"));
        motionStatusLabel_->setText(QStringLiteral("Movimento do programa: 0% • parado"));
    }

    if (!suggestion_.scene.isEmpty() && candidateFrames_.contains(suggestion_.scene)) {
        const QImage image = candidateFrames_.value(suggestion_.scene);
        suggestionPreview_->setPixmap(QPixmap::fromImage(image).scaled(suggestionPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        suggestionPreview_->setText(QString());
    } else if (!current.isEmpty() && candidateFrames_.contains(current)) {
        const QImage image = candidateFrames_.value(current);
        suggestionPreview_->setPixmap(QPixmap::fromImage(image).scaled(suggestionPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        suggestionPreview_->setText(QString());
    } else {
        QImage liveImage;
        {
            QMutexLocker lock(&frameMutex_);
            liveImage = latestFrame_.copy();
        }
        if (!liveImage.isNull()) {
            suggestionPreview_->setPixmap(QPixmap::fromImage(liveImage).scaled(suggestionPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            suggestionPreview_->setText(QString());
        } else {
            suggestionPreview_->setPixmap(QPixmap());
            suggestionPreview_->setText(QStringLiteral("SEM\nPRÉVIA"));
        }
    }

    const int hours = static_cast<int>(elapsedSeconds_ / 3600);
    const int minutes = static_cast<int>((elapsedSeconds_ % 3600) / 60);
    const int seconds = static_cast<int>(elapsedSeconds_ % 60);
    timeLabel_->setText(QStringLiteral("Tempo de transmissão\n%1:%2:%3")
                            .arg(hours, 2, 10, QLatin1Char('0'))
                            .arg(minutes, 2, 10, QLatin1Char('0'))
                            .arg(seconds, 2, 10, QLatin1Char('0')));
    cutsLabel_->setText(QStringLiteral("Cortes realizados\n%1").arg(cuts_));
    mostUsedLabel_->setText(QStringLiteral("Cena mais usada\n%1").arg(mostUsedScene_.isEmpty() ? QStringLiteral("—") : mostUsedScene_));

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
    lastCutScene_ = sceneName;
    if (recentScenes_.isEmpty() || recentScenes_.last() != sceneName)
        recentScenes_.append(sceneName);
    while (recentScenes_.size() > kMaxRecentScenes)
        recentScenes_.removeFirst();
    secondsSinceAI_ = kAIAnalysisEverySeconds;
    static std::map<QString, int> usage;
    const int count = ++usage[sceneName];
    if (count > mostUsedCount_) {
        mostUsedCount_ = count;
        mostUsedScene_ = sceneName;
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
        reasonsLabel_->setText(QStringLiteral("Corte protegido: aguarde %1s.").arg(kMinimumCutIntervalSeconds - secondsSinceCut_));
        return;
    }
    applyScene(suggestion_.scene);
}

void DiretorDock::ignoreSuggestion()
{
    if (!directorEnabled_ || !streamingActive_ || suggestion_.scene.isEmpty() || suggestion_.scene == currentSceneName())
        return;
    lastIgnoredScene_ = suggestion_.scene;
    suggestion_ = Suggestion{};
    suggestion_.reasons = QStringList() << QStringLiteral("Sugestão ignorada pelo operador. Aguardando nova evidência.");
    suggestion_.source = QStringLiteral("Operador");
    secondsSinceAI_ = kAIAnalysisEverySeconds;
    updateUi();
}


QString DiretorDock::ruleKey(const QString &trigger, const QString &target, int delay) const
{
    return trigger + QStringLiteral("\x1f") + target + QStringLiteral("\x1f") + QString::number(delay);
}

void DiretorDock::addPredefinedRule()
{
    const QStringList scenes = allSceneNames();
    const int row = rulesTable_->rowCount();
    rulesTable_->insertRow(row);

    auto *trigger = new QComboBox(rulesTable_);
    auto *target = new QComboBox(rulesTable_);
    trigger->setEditable(true);
    target->setEditable(true);
    trigger->addItems(scenes);
    target->addItems(scenes);
    if (!scenes.isEmpty()) {
        trigger->setCurrentText(scenes.first());
        target->setCurrentText(scenes.size() > 1 ? scenes.at(1) : scenes.first());
    }

    auto *delay = new QSpinBox(rulesTable_);
    delay->setRange(0, 3600);
    delay->setValue(10);
    delay->setSuffix(QStringLiteral(" s"));

    rulesTable_->setCellWidget(row, 0, trigger);
    rulesTable_->setCellWidget(row, 1, target);
    rulesTable_->setCellWidget(row, 2, delay);

    connect(trigger, &QComboBox::currentTextChanged, this, &DiretorDock::predefinedRuleCellChanged);
    connect(target, &QComboBox::currentTextChanged, this, &DiretorDock::predefinedRuleCellChanged);
    connect(delay, QOverload<int>::of(&QSpinBox::valueChanged), this, &DiretorDock::predefinedRuleCellChanged);
    savePredefinedRules();
}

void DiretorDock::removePredefinedRule()
{
    const int row = rulesTable_->currentRow();
    if (row < 0)
        return;
    rulesTable_->removeRow(row);
    triggeredRuleKeys_.clear();
    savePredefinedRules();
}

void DiretorDock::loadDefaultRules()
{
    // V7 não instala uma sequência automática por padrão. Isso seria apenas
    // uma alternância temporal e não representa uma decisão de direção.
    rulesTable_->setRowCount(0);
    triggeredRuleKeys_.clear();
    savePredefinedRules();
    setAIStatus(QStringLiteral("● SEM IA: nenhuma regra padrão instalada. Configure as decisões que deseja usar."), true);
}

void DiretorDock::predefinedRuleCellChanged()
{
    savePredefinedRules();
    triggeredRuleKeys_.clear();
}

void DiretorDock::loadPredefinedRules()
{
    rulesTable_->setRowCount(0);
    QSettings settings(QStringLiteral("DiretorDeCulto"), QStringLiteral("OBS"));
    const QByteArray raw = settings.value(QStringLiteral("predefined_rules")).toByteArray();
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
    if (!doc.isArray() || doc.array().isEmpty()) {
        setAIStatus(QStringLiteral("● SEM IA: nenhuma regra configurada — adicione as condições de direção"), true);
        return;
    }

    const QStringList scenes = allSceneNames();
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        const QString triggerName = obj.value(QStringLiteral("trigger")).toString();
        const QString targetName = obj.value(QStringLiteral("target")).toString();
        const int delayValue = std::clamp(obj.value(QStringLiteral("delay")).toInt(10), 0, 3600);
        if (triggerName.isEmpty() || targetName.isEmpty())
            continue;
        const int row = rulesTable_->rowCount();
        rulesTable_->insertRow(row);
        auto *trigger = new QComboBox(rulesTable_);
        auto *target = new QComboBox(rulesTable_);
        trigger->addItems(scenes);
        target->addItems(scenes);
        trigger->setEditable(true);
        target->setEditable(true);
        trigger->setCurrentText(triggerName);
        target->setCurrentText(targetName);
        auto *delay = new QSpinBox(rulesTable_);
        delay->setRange(0, 3600);
        delay->setValue(delayValue);
        delay->setSuffix(QStringLiteral(" s"));
        rulesTable_->setCellWidget(row, 0, trigger);
        rulesTable_->setCellWidget(row, 1, target);
        rulesTable_->setCellWidget(row, 2, delay);
        connect(trigger, &QComboBox::currentTextChanged, this, &DiretorDock::predefinedRuleCellChanged);
        connect(target, &QComboBox::currentTextChanged, this, &DiretorDock::predefinedRuleCellChanged);
        connect(delay, QOverload<int>::of(&QSpinBox::valueChanged), this, &DiretorDock::predefinedRuleCellChanged);
    }
}

void DiretorDock::savePredefinedRules()
{
    if (!rulesTable_)
        return;
    QJsonArray rules;
    for (int row = 0; row < rulesTable_->rowCount(); ++row) {
        auto *trigger = qobject_cast<QComboBox *>(rulesTable_->cellWidget(row, 0));
        auto *target = qobject_cast<QComboBox *>(rulesTable_->cellWidget(row, 1));
        auto *delay = qobject_cast<QSpinBox *>(rulesTable_->cellWidget(row, 2));
        if (!trigger || !target || !delay)
            continue;
        const QString triggerName = trigger->currentText().trimmed();
        const QString targetName = target->currentText().trimmed();
        if (triggerName.isEmpty() || targetName.isEmpty() || triggerName == targetName)
            continue;
        QJsonObject rule;
        rule.insert(QStringLiteral("trigger"), triggerName);
        rule.insert(QStringLiteral("target"), targetName);
        rule.insert(QStringLiteral("delay"), delay->value());
        rules.append(rule);
    }
    QSettings settings(QStringLiteral("DiretorDeCulto"), QStringLiteral("OBS"));
    settings.setValue(QStringLiteral("predefined_rules"), QJsonDocument(rules).toJson(QJsonDocument::Compact));
}

void DiretorDock::evaluatePredefinedRules()
{
    if (!directorEnabled_ || !streamingActive_ || engine_ != Engine::Predefinido)
        return;

    const QString current = currentSceneName();
    if (current.isEmpty())
        return;

    for (int row = 0; row < rulesTable_->rowCount(); ++row) {
        auto *trigger = qobject_cast<QComboBox *>(rulesTable_->cellWidget(row, 0));
        auto *target = qobject_cast<QComboBox *>(rulesTable_->cellWidget(row, 1));
        auto *delay = qobject_cast<QSpinBox *>(rulesTable_->cellWidget(row, 2));
        if (!trigger || !target || !delay)
            continue;

        const QString triggerName = trigger->currentText().trimmed();
        const QString targetName = target->currentText().trimmed();
        const int delaySeconds = delay->value();
        const QString key = ruleKey(triggerName, targetName, delaySeconds);

        const float rms = audioRms_.load(std::memory_order_relaxed);
        const float motion = motionScore_.load(std::memory_order_relaxed);
        const bool enoughTime = secondsInCurrentScene_ >= delaySeconds;
        const bool enoughStability = secondsSinceCut_ >= kMinimumCutIntervalSeconds;
        const bool hasActivity = (rms >= 0.008f || motion >= 0.015f);
        if (triggerName != current || targetName.isEmpty() || targetName == current ||
            triggeredRuleKeys_.contains(key) || !enoughTime || !enoughStability || !hasActivity)
            continue;

        const QStringList scenes = allSceneNames();
        if (!scenes.contains(targetName)) {
            suggestion_.scene = current;
            suggestion_.confidence = 0;
            suggestion_.reasons = QStringList() << QStringLiteral("Regra predefinida aponta para uma cena que não existe: %1").arg(targetName);
            suggestion_.source = QStringLiteral("Predefinido • erro de regra");
            continue;
        }

        triggeredRuleKeys_.insert(key);
        suggestion_.scene = targetName;
        suggestion_.confidence = 100;
        suggestion_.reasons = QStringList()
            << QStringLiteral("Regra predefinida acionada")
            << QStringLiteral("Cena atual: %1").arg(current)
            << QStringLiteral("Tempo na cena: %1 s").arg(secondsInCurrentScene_)
            << QStringLiteral("Áudio: %1%").arg(std::clamp(static_cast<int>(rms * 100.0f * 2.5f), 0, 100))
            << QStringLiteral("Movimento: %1%").arg(std::clamp(static_cast<int>(motion * 100.0f), 0, 100))
            << QStringLiteral("Intervalo mínimo entre cortes respeitado")
            << QStringLiteral("Ação: sugerir %1").arg(targetName);
        suggestion_.source = QStringLiteral("Predefinido • regra %1").arg(row + 1);
        setAIStatus(QStringLiteral("● SEM IA: regra %1 → %2").arg(row + 1).arg(targetName), true);
        updateUi();

        if (mode_ == Mode::Automatico && secondsSinceCut_ >= kMinimumCutIntervalSeconds) {
            applyScene(targetName);
        }
        break;
    }
}

void DiretorDock::engineChanged()
{
    const int id = engineGroup_ ? engineGroup_->checkedId() : static_cast<int>(Engine::IA);
    engine_ = (id == static_cast<int>(Engine::Predefinido)) ? Engine::Predefinido : Engine::IA;
    QSettings engineSettings(QStringLiteral("DiretorDeCulto"), QStringLiteral("OBS"));
    engineSettings.setValue(QStringLiteral("engine"), engine_ == Engine::Predefinido ? QStringLiteral("predefinido") : QStringLiteral("ia"));

    const bool usingAI = engine_ == Engine::IA;
    if (rulesTable_) {
        rulesTable_->parentWidget()->setVisible(true);
    }
    if (modelEdit_)
        modelEdit_->setEnabled(usingAI);
    if (apiKeyEdit_)
        apiKeyEdit_->setEnabled(usingAI);
    if (testAIButton_)
        testAIButton_->setEnabled(usingAI);
    if (analyzeButton_)
        analyzeButton_->setEnabled(usingAI);
    if (addRuleButton_)
        addRuleButton_->setEnabled(!usingAI);
    if (removeRuleButton_)
        removeRuleButton_->setEnabled(!usingAI);
    if (defaultRulesButton_)
        defaultRulesButton_->setEnabled(!usingAI);

    triggeredRuleKeys_.clear();
    secondsInCurrentScene_ = 0;
    observedScene_ = currentSceneName();
    suggestion_ = Suggestion{};

    if (usingAI) {
        if (directorEnabled_ && streamingActive_)
            startLoop();
        setAIStatus(QStringLiteral("● IA: %1").arg(apiKey().isEmpty() ? QStringLiteral("aguardando API Key") : modelName()), !apiKey().isEmpty());
    } else {
        if (directorEnabled_ && streamingActive_)
            startLoop();
        setAIStatus(QStringLiteral("● SEM IA: regras + condições locais"), true);
    }
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
    // Manual também analisa. A diferença é somente a ação: nunca corta sozinho.
    updateUi();
}

void DiretorDock::setStatus(const QString &text, bool active)
{
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(active
                                    ? QStringLiteral("color:#27e68a;font-weight:900;")
                                    : QStringLiteral("color:#ffc04a;font-weight:900;"));
}

void DiretorDock::setAIStatus(const QString &text, bool online)
{
    aiOnline_ = online;
    aiStatusLabel_->setText(text);
    aiStatusLabel_->setStyleSheet(online
                                      ? QStringLiteral("color:#55f0a0;font-weight:800;")
                                      : QStringLiteral("color:#ffc04a;font-weight:800;"));
}

void DiretorDock::updateProgress()
{
    if (!directorEnabled_ || !streamingActive_) {
        progressBar_->setValue(0);
        return;
    }
    if (engine_ == Engine::Predefinido) {
        progressBar_->setRange(0, 100);
        const int v = std::clamp(static_cast<int>((secondsInCurrentScene_ % 10) * 10), 0, 100);
        progressBar_->setValue(v);
        nextAnalysisLabel_->setText(QStringLiteral("Motor predefinido: avaliando cena + tempo + áudio + movimento"));
        return;
    }
    progressBar_->setRange(0, kAIAnalysisEverySeconds * 10);
    const int elapsedTenths = static_cast<int>((secondsSinceAI_ * 10) % (kAIAnalysisEverySeconds * 10));
    progressBar_->setValue(std::clamp(elapsedTenths, 0, kAIAnalysisEverySeconds * 10));
    if (aiCooldownSeconds_ > 0) {
        nextAnalysisLabel_->setText(QStringLiteral("IA em espera após erro/limite: %1s").arg(aiCooldownSeconds_));
        return;
    }
    const int remaining = (secondsSinceAI_ < kAIAnalysisEverySeconds)
                              ? (kAIAnalysisEverySeconds - static_cast<int>(secondsSinceAI_))
                              : 0;
    nextAnalysisLabel_->setText(remaining > 0 ? QStringLiteral("Próxima análise em %1s").arg(remaining)
                                              : (aiBusy_ ? QStringLiteral("IA analisando múltiplos sinais...") : QStringLiteral("Pronto para nova análise")));
}

