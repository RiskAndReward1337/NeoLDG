#include "MainWindow.hpp"

#include "LdgTunerController.hpp"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QDateTime>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QCloseEvent>
#include <QtSerialPort/QSerialPortInfo>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxMetricHistorySamples = 50000;
constexpr int kTrimmedMetricHistorySamples = 40000;
constexpr int kMeterAnimationIntervalMs = 16;

int powerBarValue(double watts, double scaleWatts)
{
    const double safeScale = std::max(1.0, scaleWatts);
    const double safeWatts = std::clamp(watts, 0.0, safeScale);
    return static_cast<int>((std::sqrt(1.0 + safeWatts) / std::sqrt(1.0 + safeScale)) * 1000.0);
}

int swrBarValue(double swr)
{
    const double safeSWR = std::clamp(swr, 1.0, 4.0);
    return static_cast<int>(((safeSWR - 1.0) / 3.0) * 1000.0);
}

double animateToward(double current, double target, double dtMs, double timeConstantMs)
{
    if (timeConstantMs <= 0.0) {
        return target;
    }

    const double alpha = 1.0 - std::exp(-dtMs / timeConstantMs);
    return current + (target - current) * alpha;
}

QString formatWatts(double watts)
{
    return QStringLiteral("%1 W").arg(QString::number(watts, 'f', 1));
}

QString formatRaw(quint16 value)
{
    return QStringLiteral("0x%1").arg(QString::number(value, 16).toUpper().rightJustified(4, QChar('0')));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , controller_(new LdgTunerController(this))
    , settings_(QStringLiteral("NeoLDG"), QStringLiteral("NeoLDG"))
    , peakDecayTimer_(new QTimer(this))
{
    buildUi();
    applyTheme();
    bindSignals();
    refreshPorts();
    restoreSettings();

    connectionValue_->setText(QStringLiteral("Not connected"));
    antennaValue_->setText(QStringLiteral("--"));
    modeValue_->setText(QStringLiteral("Auto"));
    updateConnectionUi(false);
    updateBypassButtonText();
    setConnectionStatus(QStringLiteral("DISCONNECTED"), QStringLiteral("idle"));
    setTuneOutcome(neoldg::TuneOutcome::Idle, QStringLiteral("Idle"));
    recomputeTelemetryForSettings();
    updateMetricHistoryUi();

    peakDecayTimer_->setInterval(kMeterAnimationIntervalMs);
    connect(peakDecayTimer_, &QTimer::timeout, this, &MainWindow::animateTelemetry);
    peakDecayTimer_->start();

    QTimer::singleShot(0, this, [this]() {
        if (autoConnectCheck_->isChecked() && portCombo_->currentIndex() >= 0) {
            connectButton_->click();
        }
    });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("window"));
    setCentralWidget(root);
    setWindowTitle(QStringLiteral("NeoLDG"));
    resize(1280, 860);

    auto* outerLayout = new QVBoxLayout(root);
    outerLayout->setContentsMargins(24, 24, 24, 24);
    outerLayout->setSpacing(18);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(16);
    outerLayout->addLayout(headerLayout);

    auto* titleColumn = new QVBoxLayout();
    titleColumn->setSpacing(4);
    headerLayout->addLayout(titleColumn, 1);

    auto* title = new QLabel(QStringLiteral("NeoLDG"));
    title->setObjectName(QStringLiteral("title"));
    titleColumn->addWidget(title);

    auto* pillRow = new QHBoxLayout();
    pillRow->setSpacing(10);
    headerLayout->addLayout(pillRow);

    layoutToggleButton_ = new QPushButton(QStringLiteral("Minimal Layout"));
    pillRow->addWidget(layoutToggleButton_);

    connectionPill_ = new QLabel(QStringLiteral("DISCONNECTED"));
    connectionPill_->setObjectName(QStringLiteral("statusPill"));
    pillRow->addWidget(connectionPill_);

    tunePill_ = new QLabel(QStringLiteral("IDLE"));
    tunePill_->setObjectName(QStringLiteral("statusPill"));
    pillRow->addWidget(tunePill_);

    mainLayout_ = new QHBoxLayout();
    mainLayout_->setSpacing(18);
    outerLayout->addLayout(mainLayout_, 1);

    leftColumnLayout_ = new QVBoxLayout();
    leftColumnLayout_->setSpacing(18);
    mainLayout_->addLayout(leftColumnLayout_, 7);

    rightColumnLayout_ = new QVBoxLayout();
    rightColumnLayout_->setSpacing(18);
    mainLayout_->addLayout(rightColumnLayout_, 5);

    QVBoxLayout* heroBody = nullptr;
    heroCard_ = createCard(QStringLiteral("Live Telemetry"), heroBody);
    leftColumnLayout_->addWidget(heroCard_);

    auto* heroTop = new QHBoxLayout();
    heroTop->setSpacing(20);
    heroBody->addLayout(heroTop);

    auto* heroBandColumn = new QVBoxLayout();
    heroBandColumn->setSpacing(6);
    heroTop->addLayout(heroBandColumn, 1);

    bandValue_ = new QLabel(QStringLiteral("Unknown"));
    bandValue_->setObjectName(QStringLiteral("heroBand"));
    heroBandColumn->addWidget(bandValue_);

    rawKeyValue_ = new QLabel(QStringLiteral("Band key 0x0000"));
    rawKeyValue_->setObjectName(QStringLiteral("heroRaw"));
    heroBandColumn->addWidget(rawKeyValue_);

    auto* detailGrid = new QGridLayout();
    detailGrid->setHorizontalSpacing(16);
    detailGrid->setVerticalSpacing(10);
    heroTop->addLayout(detailGrid, 1);

    auto makeDetailLabel = []() {
        auto* label = new QLabel();
        label->setObjectName(QStringLiteral("detailValue"));
        return label;
    };

    auto* connKey = new QLabel(QStringLiteral("Connection"));
    connKey->setObjectName(QStringLiteral("detailKey"));
    detailGrid->addWidget(connKey, 0, 0);
    connectionValue_ = makeDetailLabel();
    detailGrid->addWidget(connectionValue_, 0, 1);

    auto* antKey = new QLabel(QStringLiteral("Antenna"));
    antKey->setObjectName(QStringLiteral("detailKey"));
    detailGrid->addWidget(antKey, 1, 0);
    antennaValue_ = makeDetailLabel();
    detailGrid->addWidget(antennaValue_, 1, 1);

    auto* modeKey = new QLabel(QStringLiteral("Tune Mode"));
    modeKey->setObjectName(QStringLiteral("detailKey"));
    detailGrid->addWidget(modeKey, 2, 0);
    modeValue_ = makeDetailLabel();
    detailGrid->addWidget(modeValue_, 2, 1);

    auto* rawFwdKey = new QLabel(QStringLiteral("Forward Raw"));
    rawFwdKey->setObjectName(QStringLiteral("detailKey"));
    detailGrid->addWidget(rawFwdKey, 3, 0);
    rawFwdValue_ = makeDetailLabel();
    detailGrid->addWidget(rawFwdValue_, 3, 1);

    auto* rawRefKey = new QLabel(QStringLiteral("Reflected Raw"));
    rawRefKey->setObjectName(QStringLiteral("detailKey"));
    detailGrid->addWidget(rawRefKey, 4, 0);
    rawRefValue_ = makeDetailLabel();
    detailGrid->addWidget(rawRefValue_, 4, 1);

    metricsRow_ = new QWidget();
    auto* metricsLayout = new QHBoxLayout(metricsRow_);
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setSpacing(18);
    leftColumnLayout_->addWidget(metricsRow_);

    metricsLayout->addWidget(createMetricTile(QStringLiteral("Forward Power"), QStringLiteral("teal"), fwdValue_, fwdBar_));
    metricsLayout->addWidget(createMetricTile(QStringLiteral("Reflected Power"), QStringLiteral("sunset"), refValue_, refBar_));
    metricsLayout->addWidget(createMetricTile(QStringLiteral("SWR"), QStringLiteral("lime"), swrValue_, swrBar_));

    QVBoxLayout* logBody = nullptr;
    logCard_ = createCard(QStringLiteral("Activity"), logBody);
    leftColumnLayout_->addWidget(logCard_, 1);

    logList_ = new QListWidget();
    logList_->setObjectName(QStringLiteral("logList"));
    logList_->setSpacing(6);
    logBody->addWidget(logList_);

    QVBoxLayout* connectionBody = nullptr;
    connectionCard_ = createCard(QStringLiteral("Connection"), connectionBody);
    rightColumnLayout_->addWidget(connectionCard_);

    auto* portLabel = new QLabel(QStringLiteral("Serial Port"));
    portLabel->setObjectName(QStringLiteral("fieldLabel"));
    connectionBody->addWidget(portLabel);

    portCombo_ = new QComboBox();
    portCombo_->setObjectName(QStringLiteral("fieldInput"));
    connectionBody->addWidget(portCombo_);

    auto* connectionButtons = new QHBoxLayout();
    connectionButtons->setSpacing(10);
    connectionBody->addLayout(connectionButtons);

    refreshButton_ = new QPushButton(QStringLiteral("Refresh Ports"));
    connectionButtons->addWidget(refreshButton_);

    connectButton_ = new QPushButton(QStringLiteral("Connect"));
    connectButton_->setProperty("accent", true);
    connectionButtons->addWidget(connectButton_);

    autoConnectCheck_ = new QCheckBox(QStringLiteral("Connect automatically on launch"));
    autoConnectCheck_->setObjectName(QStringLiteral("checkbox"));
    connectionBody->addWidget(autoConnectCheck_);

    QVBoxLayout* controlsBody = nullptr;
    controlsCard_ = createCard(QStringLiteral("Controls"), controlsBody);
    rightColumnLayout_->addWidget(controlsCard_);

    auto* controlsGrid = new QGridLayout();
    controlsGrid->setHorizontalSpacing(10);
    controlsGrid->setVerticalSpacing(10);
    controlsBody->addLayout(controlsGrid);

    memoryTuneButton_ = new QPushButton(QStringLiteral("Memory Tune"));
    memoryTuneButton_->setProperty("accent", true);
    controlsGrid->addWidget(memoryTuneButton_, 0, 0);

    fullTuneButton_ = new QPushButton(QStringLiteral("Full Tune"));
    fullTuneButton_->setProperty("accent", true);
    controlsGrid->addWidget(fullTuneButton_, 0, 1);

    antennaButton_ = new QPushButton(QStringLiteral("Toggle Antenna"));
    controlsGrid->addWidget(antennaButton_, 1, 0);

    bypassButton_ = new QPushButton();
    controlsGrid->addWidget(bypassButton_, 1, 1);

    autoModeButton_ = new QPushButton(QStringLiteral("Auto Mode"));
    controlsGrid->addWidget(autoModeButton_, 2, 0);

    manualModeButton_ = new QPushButton(QStringLiteral("Manual Mode"));
    controlsGrid->addWidget(manualModeButton_, 2, 1);

    QVBoxLayout* settingsBody = nullptr;
    settingsCard_ = createCard(QStringLiteral("Meter Settings"), settingsBody);
    rightColumnLayout_->addWidget(settingsCard_);

    auto* modelLabel = new QLabel(QStringLiteral("Tuner Model"));
    modelLabel->setObjectName(QStringLiteral("fieldLabel"));
    settingsBody->addWidget(modelLabel);

    modelCombo_ = new QComboBox();
    modelCombo_->setObjectName(QStringLiteral("fieldInput"));
    modelCombo_->addItem(
        neoldg::tunerModelDisplayName(neoldg::TunerModel::Ldg1000ProII),
        static_cast<int>(neoldg::TunerModel::Ldg1000ProII));
    modelCombo_->addItem(
        neoldg::tunerModelDisplayName(neoldg::TunerModel::Ldg600ProII),
        static_cast<int>(neoldg::TunerModel::Ldg600ProII));
    settingsBody->addWidget(modelCombo_);

    auto* voltageLabel = new QLabel(QStringLiteral("Supply Voltage"));
    voltageLabel->setObjectName(QStringLiteral("fieldLabel"));
    settingsBody->addWidget(voltageLabel);

    voltageCombo_ = new QComboBox();
    voltageCombo_->setObjectName(QStringLiteral("fieldInput"));
    const QList<double> voltages = {12.0, 12.5, 13.0, 13.5, 13.8, 14.3};
    for (double voltage : voltages) {
        voltageCombo_->addItem(QStringLiteral("%1 V").arg(QString::number(voltage, 'f', 1)), voltage);
    }
    settingsBody->addWidget(voltageCombo_);

    peakHoldCheck_ = new QCheckBox(QStringLiteral("Peak hold for forward/reflected power and SWR"));
    peakHoldCheck_->setObjectName(QStringLiteral("checkbox"));
    settingsBody->addWidget(peakHoldCheck_);

    auto* peakHoldLabel = new QLabel(QStringLiteral("Peak Hold Time"));
    peakHoldLabel->setObjectName(QStringLiteral("fieldLabel"));
    settingsBody->addWidget(peakHoldLabel);

    peakHoldSpin_ = new QDoubleSpinBox();
    peakHoldSpin_->setRange(0.0, 5.0);
    peakHoldSpin_->setSingleStep(0.05);
    peakHoldSpin_->setDecimals(2);
    peakHoldSpin_->setSuffix(QStringLiteral(" s"));
    peakHoldSpin_->setObjectName(QStringLiteral("fieldInput"));
    settingsBody->addWidget(peakHoldSpin_);

    QVBoxLayout* metricsCaptureBody = nullptr;
    metricsCaptureCard_ = createCard(QStringLiteral("Metrics Export"), metricsCaptureBody);
    rightColumnLayout_->addWidget(metricsCaptureCard_);

    auto* metricsHelp = new QLabel(QStringLiteral("NeoLDG keeps a rolling in-memory history of forward power, reflected power, and SWR samples. Export them as CSV for graphing."));
    metricsHelp->setObjectName(QStringLiteral("helperText"));
    metricsHelp->setWordWrap(true);
    metricsCaptureBody->addWidget(metricsHelp);

    auto* metricsCountKey = new QLabel(QStringLiteral("Buffered Samples"));
    metricsCountKey->setObjectName(QStringLiteral("fieldLabel"));
    metricsCaptureBody->addWidget(metricsCountKey);

    metricsCountValue_ = new QLabel(QStringLiteral("0 samples"));
    metricsCountValue_->setObjectName(QStringLiteral("detailValue"));
    metricsCaptureBody->addWidget(metricsCountValue_);

    auto* metricsButtons = new QHBoxLayout();
    metricsButtons->setSpacing(10);
    metricsCaptureBody->addLayout(metricsButtons);

    exportMetricsButton_ = new QPushButton(QStringLiteral("Export CSV"));
    exportMetricsButton_->setProperty("accent", true);
    metricsButtons->addWidget(exportMetricsButton_);

    clearMetricsButton_ = new QPushButton(QStringLiteral("Clear History"));
    metricsButtons->addWidget(clearMetricsButton_);

    rightColumnSpacer_ = new QWidget();
    rightColumnSpacer_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    rightColumnLayout_->addWidget(rightColumnSpacer_);
}

void MainWindow::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
QWidget {
    color: #E7F1F7;
    font-family: "IBM Plex Sans", "Noto Sans", "Segoe UI", sans-serif;
    font-size: 10pt;
}

QWidget#window {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 #091218,
        stop:0.45 #0F1C24,
        stop:1 #11252D);
}

QFrame#card {
    background: rgba(13, 23, 30, 0.90);
    border: 1px solid rgba(129, 160, 179, 0.18);
    border-radius: 20px;
}

QLabel#title {
    font-size: 28pt;
    font-weight: 700;
    color: #F7FBFF;
}

QLabel#sectionTitle {
    font-size: 12pt;
    font-weight: 650;
    color: #F4FBFF;
}

QLabel#statusPill {
    padding: 8px 14px;
    border-radius: 999px;
    font-weight: 700;
    background: rgba(37, 54, 66, 0.75);
    border: 1px solid rgba(110, 145, 165, 0.24);
}

QLabel#statusPill[tone="idle"] {
    color: #B9CBD8;
}

QLabel#statusPill[tone="busy"] {
    color: #FFE1A3;
    background: rgba(88, 63, 18, 0.85);
    border-color: rgba(205, 153, 53, 0.45);
}

QLabel#statusPill[tone="good"] {
    color: #AAF0C7;
    background: rgba(17, 63, 42, 0.88);
    border-color: rgba(50, 151, 103, 0.42);
}

QLabel#statusPill[tone="warn"] {
    color: #FFD89D;
    background: rgba(87, 60, 17, 0.88);
    border-color: rgba(205, 147, 42, 0.42);
}

QLabel#statusPill[tone="bad"] {
    color: #FFC4B8;
    background: rgba(87, 28, 28, 0.90);
    border-color: rgba(205, 77, 77, 0.42);
}

QLabel#heroBand {
    font-size: 34pt;
    font-weight: 750;
    color: #C9FF8D;
}

QLabel#heroRaw {
    color: #6FD4FF;
    font-size: 11pt;
    font-weight: 600;
}

QLabel#detailKey {
    color: #7D93A4;
    font-size: 9pt;
    text-transform: uppercase;
}

QLabel#detailValue {
    color: #F4FBFF;
    font-size: 11pt;
    font-weight: 600;
}

QLabel#metricLabel {
    color: #87A0B2;
    font-size: 9pt;
    font-weight: 600;
}

QLabel#metricValue {
    color: #F8FDFF;
    font-size: 22pt;
    font-weight: 750;
}

QPushButton {
    min-height: 44px;
    border-radius: 14px;
    border: 1px solid rgba(119, 148, 164, 0.22);
    background: rgba(26, 40, 49, 0.92);
    color: #EDF7FD;
    font-weight: 650;
    padding: 0 14px;
}

QPushButton:hover {
    background: rgba(33, 52, 64, 0.96);
}

QPushButton:pressed {
    background: rgba(22, 35, 43, 0.96);
}

QPushButton[accent="true"] {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 #157A68,
        stop:1 #2A9C77);
    border-color: rgba(90, 212, 167, 0.35);
}

QPushButton[accent="true"]:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 #1A8C77,
        stop:1 #34AE86);
}

QPushButton:disabled {
    color: #6F8595;
    background: rgba(17, 25, 31, 0.88);
    border-color: rgba(88, 108, 120, 0.12);
}

QComboBox,
QDoubleSpinBox {
    min-height: 42px;
    padding: 0 12px;
    border-radius: 14px;
    background: rgba(12, 19, 25, 0.95);
    border: 1px solid rgba(111, 144, 163, 0.22);
}

QComboBox::drop-down,
QDoubleSpinBox::drop-down {
    width: 30px;
    border: 0;
}

QAbstractItemView {
    background: #0F1A21;
    border: 1px solid rgba(111, 144, 163, 0.22);
    selection-background-color: #1C3542;
    selection-color: #F4FBFF;
}

QCheckBox {
    color: #D7E4ED;
    spacing: 10px;
}

QCheckBox::indicator {
    width: 18px;
    height: 18px;
    border-radius: 5px;
    border: 1px solid rgba(113, 144, 161, 0.28);
    background: rgba(11, 18, 23, 0.96);
}

QCheckBox::indicator:checked {
    background: #2C9D78;
    border-color: #39C58E;
}

QLabel#fieldLabel {
    color: #8AA0B1;
    font-size: 9pt;
    font-weight: 600;
}

QLabel#helperText {
    color: #9AB0C0;
    line-height: 1.4;
}

QProgressBar {
    min-height: 12px;
    border-radius: 8px;
    background: rgba(9, 15, 20, 0.96);
    border: 1px solid rgba(90, 110, 123, 0.15);
    text-align: center;
}

QProgressBar::chunk {
    border-radius: 7px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #29B7A7,
        stop:1 #8AF091);
}

QProgressBar[tone="sunset"]::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #D9823F,
        stop:1 #FFC167);
}

QProgressBar[tone="lime"]::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #76D45E,
        stop:1 #CFFF7C);
}

QProgressBar[tone="warn"]::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #D18A34,
        stop:1 #F7CE71);
}

QProgressBar[tone="bad"]::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #B74D4D,
        stop:1 #F08C78);
}

QListWidget#logList {
    background: transparent;
    border: 0;
    outline: none;
    padding: 2px;
}

QListWidget#logList::item {
    background: rgba(14, 21, 27, 0.90);
    border: 1px solid rgba(93, 116, 129, 0.12);
    border-radius: 12px;
    padding: 10px 12px;
    margin: 2px 0;
}
    )"));
}

void MainWindow::bindSignals()
{
    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(layoutToggleButton_, &QPushButton::clicked, this, [this]() {
        applyLayoutMode(layoutMode_ == LayoutMode::Full ? LayoutMode::Minimal : LayoutMode::Full);
    });

    connect(connectButton_, &QPushButton::clicked, this, [this]() {
        if (controller_->isConnected()) {
            controller_->disconnectSerial();
            return;
        }

        const QString portName = portCombo_->currentData().toString();
        if (portName.isEmpty()) {
            appendLog(QStringLiteral("No serial port selected."), true);
            return;
        }

        setConnectionStatus(QStringLiteral("CONNECTING"), QStringLiteral("busy"));
        controller_->connectSerial(portName);
    });

    connect(memoryTuneButton_, &QPushButton::clicked, controller_, &LdgTunerController::memoryTune);
    connect(fullTuneButton_, &QPushButton::clicked, controller_, &LdgTunerController::fullTune);
    connect(antennaButton_, &QPushButton::clicked, controller_, &LdgTunerController::toggleAntenna);
    connect(bypassButton_, &QPushButton::clicked, controller_, &LdgTunerController::toggleBypass);
    connect(autoModeButton_, &QPushButton::clicked, controller_, &LdgTunerController::setAutoTuneMode);
    connect(manualModeButton_, &QPushButton::clicked, controller_, &LdgTunerController::setManualTuneMode);

    connect(controller_, &LdgTunerController::connectionChanged, this, [this](bool connected, const QString& portName) {
        updateConnectionUi(connected);
        connectionValue_->setText(connected ? portName : QStringLiteral("Not connected"));

        if (connected) {
            setConnectionStatus(QStringLiteral("CONNECTED"), QStringLiteral("good"));
        } else {
            setConnectionStatus(QStringLiteral("DISCONNECTED"), QStringLiteral("idle"));
            setTuneOutcome(neoldg::TuneOutcome::Idle, QStringLiteral("Idle"));
        }
    });

    connect(controller_, &LdgTunerController::busyChanged, this, [this](bool busy) {
        if (busy) {
            setConnectionStatus(QStringLiteral("BUSY"), QStringLiteral("busy"));
        } else if (controller_->isConnected()) {
            setConnectionStatus(QStringLiteral("CONNECTED"), QStringLiteral("good"));
        }
        updateActionButtons();
    });

    connect(controller_, &LdgTunerController::meterSampleUpdated, this, &MainWindow::onMeterSampleUpdated);

    connect(controller_, &LdgTunerController::tuneOutcomeChanged, this, [this](neoldg::TuneOutcome outcome, const QString& label) {
        setTuneOutcome(outcome, label);
    });

    connect(controller_, &LdgTunerController::commandCompleted, this, &MainWindow::onCommandCompleted);

    connect(controller_, &LdgTunerController::statusMessage, this, [this](const QString& message, bool error) {
        appendLog(message, error);
    });

    connect(controller_, &LdgTunerController::logMessage, this, [this](const QString& message) {
        appendLog(message);
    });

    connect(exportMetricsButton_, &QPushButton::clicked, this, &MainWindow::exportMetricHistory);
    connect(clearMetricsButton_, &QPushButton::clicked, this, &MainWindow::clearMetricHistory);

    connect(modelCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        recomputeTelemetryForSettings();
        appendLog(QStringLiteral("Using %1 meter calibration.")
            .arg(neoldg::tunerModelDisplayName(selectedTunerModel())));
    });

    connect(voltageCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        recomputeTelemetryForSettings();
    });

    connect(peakHoldCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        peakHoldSpin_->setEnabled(enabled);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        forwardHoldUntilMs_ = now;
        reflectedHoldUntilMs_ = now;
        swrHoldUntilMs_ = now;
        if (!enabled) {
            displayedForward_ = liveTelemetry_.forwardWatts;
            displayedReflected_ = liveTelemetry_.reflectedWatts;
            displayedSwr_ = liveTelemetry_.swr;
            refreshTelemetryDisplay();
        }
    });
}

void MainWindow::refreshPorts()
{
    const QString currentPort = portCombo_->currentData().toString();
    const QString savedPort = settings_.value(QStringLiteral("serial/port")).toString();

    QSignalBlocker blocker(portCombo_);
    portCombo_->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto& info : ports) {
        QString label = info.portName();
        if (!info.description().isEmpty()) {
            label += QStringLiteral("  |  %1").arg(info.description());
        }
        portCombo_->addItem(label, info.portName());
    }

    auto selectPort = [this](const QString& portName) {
        if (portName.isEmpty()) {
            return false;
        }
        for (int i = 0; i < portCombo_->count(); ++i) {
            if (portCombo_->itemData(i).toString() == portName) {
                portCombo_->setCurrentIndex(i);
                return true;
            }
        }
        return false;
    };

    if (!selectPort(currentPort)) {
        selectPort(savedPort);
    }

    if (portCombo_->count() == 0) {
        portCombo_->addItem(QStringLiteral("No serial ports detected"), QString());
        portCombo_->setCurrentIndex(0);
    }

    updateActionButtons();
}

void MainWindow::restoreSettings()
{
    restoreGeometry(settings_.value(QStringLiteral("window/geometry")).toByteArray());

    const QString savedLayout = settings_.value(QStringLiteral("layout/mode"), QStringLiteral("full")).toString();
    applyLayoutMode(savedLayout == QStringLiteral("minimal") ? LayoutMode::Minimal : LayoutMode::Full);

    {
        QSignalBlocker blocker(modelCombo_);
        const auto savedModel = neoldg::tunerModelFromStorageKey(
            settings_.value(QStringLiteral("tuner/model"),
                neoldg::tunerModelStorageKey(neoldg::TunerModel::Ldg1000ProII)).toString());

        for (int i = 0; i < modelCombo_->count(); ++i) {
            if (modelCombo_->itemData(i).toInt() == static_cast<int>(savedModel)) {
                modelCombo_->setCurrentIndex(i);
                break;
            }
        }
    }

    autoConnectCheck_->setChecked(settings_.value(QStringLiteral("serial/autoConnect"), false).toBool());
    peakHoldCheck_->setChecked(settings_.value(QStringLiteral("meter/peakHold"), true).toBool());
    peakHoldSpin_->setValue(settings_.value(QStringLiteral("meter/peakHoldSeconds"), 0.25).toDouble());
    peakHoldSpin_->setEnabled(peakHoldCheck_->isChecked());

    {
        QSignalBlocker blocker(voltageCombo_);
        const double savedVoltage = settings_.value(QStringLiteral("meter/supplyVoltage"), 13.8).toDouble();
        for (int i = 0; i < voltageCombo_->count(); ++i) {
            if (std::abs(voltageCombo_->itemData(i).toDouble() - savedVoltage) < 0.001) {
                voltageCombo_->setCurrentIndex(i);
                break;
            }
        }
    }
}

void MainWindow::saveSettings()
{
    settings_.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings_.setValue(QStringLiteral("layout/mode"),
        layoutMode_ == LayoutMode::Minimal ? QStringLiteral("minimal") : QStringLiteral("full"));
    settings_.setValue(QStringLiteral("serial/port"), portCombo_->currentData().toString());
    settings_.setValue(QStringLiteral("serial/autoConnect"), autoConnectCheck_->isChecked());
    settings_.setValue(QStringLiteral("tuner/model"), neoldg::tunerModelStorageKey(selectedTunerModel()));
    settings_.setValue(QStringLiteral("meter/supplyVoltage"), selectedSupplyVoltage());
    settings_.setValue(QStringLiteral("meter/peakHold"), peakHoldCheck_->isChecked());
    settings_.setValue(QStringLiteral("meter/peakHoldSeconds"), selectedPeakHoldSeconds());
}

void MainWindow::applyLayoutMode(LayoutMode mode)
{
    layoutMode_ = mode;

    const bool minimal = layoutMode_ == LayoutMode::Minimal;
    if (mainLayout_ != nullptr) {
        mainLayout_->setDirection(minimal ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    }
    if (heroCard_ != nullptr) {
        heroCard_->setVisible(!minimal);
    }
    if (logCard_ != nullptr) {
        logCard_->setVisible(!minimal);
    }
    if (connectionCard_ != nullptr) {
        connectionCard_->setVisible(!minimal);
    }
    if (settingsCard_ != nullptr) {
        settingsCard_->setVisible(!minimal);
    }
    if (metricsCaptureCard_ != nullptr) {
        metricsCaptureCard_->setVisible(!minimal);
    }
    if (rightColumnSpacer_ != nullptr) {
        rightColumnSpacer_->setVisible(!minimal);
    }

    layoutToggleButton_->setText(minimal ? QStringLiteral("Full Layout") : QStringLiteral("Minimal Layout"));
    layoutToggleButton_->setProperty("accent", minimal);
    layoutToggleButton_->style()->unpolish(layoutToggleButton_);
    layoutToggleButton_->style()->polish(layoutToggleButton_);

    if (minimal) {
        resize(980, 600);
    }
}

void MainWindow::updateConnectionUi(bool connected)
{
    portCombo_->setEnabled(!connected && !controller_->isBusy() && !portCombo_->currentData().toString().isEmpty());
    refreshButton_->setEnabled(!connected && !controller_->isBusy());
    connectButton_->setText(connected ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
    connectButton_->setProperty("accent", !connected);
    connectButton_->style()->unpolish(connectButton_);
    connectButton_->style()->polish(connectButton_);
    updateActionButtons();
}

void MainWindow::animateTelemetry()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    double dtMs = static_cast<double>(kMeterAnimationIntervalMs);
    if (lastAnimationTickMs_ > 0) {
        dtMs = static_cast<double>(std::max<qint64>(1, now - lastAnimationTickMs_));
    }
    lastAnimationTickMs_ = now;

    const bool peakHoldEnabled = peakHoldCheck_->isChecked();
    bool changed = false;

    const auto animateMetric = [&](double& displayed, double live, qint64 holdUntilMs, double attackMs, double releaseMs, double epsilon) {
        const bool holdingPeak = peakHoldEnabled && live < displayed && now < holdUntilMs;
        if (holdingPeak) {
            return false;
        }

        const double target = live;
        const double timeConstantMs = target >= displayed ? attackMs : releaseMs;
        double next = animateToward(displayed, target, dtMs, timeConstantMs);
        if (std::abs(target - next) < epsilon) {
            next = target;
        }

        if (std::abs(next - displayed) < 0.0001) {
            return false;
        }

        displayed = next;
        return true;
    };

    changed |= animateMetric(displayedForward_, liveTelemetry_.forwardWatts, forwardHoldUntilMs_, 60.0, 140.0, 0.05);
    changed |= animateMetric(displayedReflected_, liveTelemetry_.reflectedWatts, reflectedHoldUntilMs_, 60.0, 140.0, 0.03);
    changed |= animateMetric(displayedSwr_, liveTelemetry_.swr, swrHoldUntilMs_, 70.0, 160.0, 0.01);

    if (changed) {
        refreshTelemetryDisplay();
    }
}

void MainWindow::updateActionButtons()
{
    const bool hasPort = !portCombo_->currentData().toString().isEmpty();
    const bool connected = controller_->isConnected();
    const bool busy = controller_->isBusy();

    connectButton_->setEnabled(connected || (!busy && hasPort));

    const bool enableCommands = connected && !busy;
    memoryTuneButton_->setEnabled(enableCommands);
    fullTuneButton_->setEnabled(enableCommands);
    antennaButton_->setEnabled(enableCommands);
    bypassButton_->setEnabled(enableCommands);
    autoModeButton_->setEnabled(enableCommands);
    manualModeButton_->setEnabled(enableCommands);
}

void MainWindow::refreshTelemetryDisplay()
{
    bandValue_->setText(liveTelemetry_.bandName);
    rawKeyValue_->setText(QStringLiteral("Band key %1").arg(formatRaw(liveSample_.bandKey)));
    rawFwdValue_->setText(formatRaw(liveSample_.forwardRaw));
    rawRefValue_->setText(formatRaw(liveSample_.reflectedRaw));

    fwdValue_->setText(formatWatts(displayedForward_));
    refValue_->setText(formatWatts(displayedReflected_));
    swrValue_->setText(QStringLiteral("%1 : 1").arg(QString::number(displayedSwr_, 'f', 1)));

    const double powerScale = neoldg::powerDisplayScaleForModel(selectedTunerModel());
    fwdBar_->setValue(powerBarValue(displayedForward_, powerScale));
    refBar_->setValue(powerBarValue(displayedReflected_, powerScale));
    swrBar_->setValue(swrBarValue(displayedSwr_));

    if (displayedSwr_ <= 1.7) {
        swrBar_->setProperty("tone", QStringLiteral("lime"));
    } else if (displayedSwr_ <= 2.0) {
        swrBar_->setProperty("tone", QStringLiteral("warn"));
    } else {
        swrBar_->setProperty("tone", QStringLiteral("bad"));
    }
    swrBar_->style()->unpolish(swrBar_);
    swrBar_->style()->polish(swrBar_);
}

void MainWindow::recomputeTelemetryForSettings()
{
    liveTelemetry_ = neoldg::computeDisplayTelemetry(
        stabilizedMeterSample(liveSample_),
        selectedSupplyVoltage(),
        selectedTunerModel());
    displayedForward_ = liveTelemetry_.forwardWatts;
    displayedReflected_ = liveTelemetry_.reflectedWatts;
    displayedSwr_ = liveTelemetry_.swr;
    refreshTelemetryDisplay();
}

void MainWindow::recordMetricHistorySample()
{
    if (metricHistory_.size() >= kMaxMetricHistorySamples) {
        metricHistory_.remove(0, metricHistory_.size() - kTrimmedMetricHistorySamples + 1);
    }

    MetricHistorySample sample;
    sample.timestampMs = QDateTime::currentMSecsSinceEpoch();
    sample.forwardWatts = liveTelemetry_.forwardWatts;
    sample.reflectedWatts = liveTelemetry_.reflectedWatts;
    sample.swr = liveTelemetry_.swr;
    sample.bandKey = liveTelemetry_.bandKey;
    sample.bandName = liveTelemetry_.bandName;
    metricHistory_.append(sample);
}

void MainWindow::updateMetricHistoryUi()
{
    if (metricsCountValue_ != nullptr) {
        metricsCountValue_->setText(QStringLiteral("%1 samples").arg(metricHistory_.size()));
    }

    const bool hasSamples = !metricHistory_.isEmpty();
    if (exportMetricsButton_ != nullptr) {
        exportMetricsButton_->setEnabled(hasSamples);
    }
    if (clearMetricsButton_ != nullptr) {
        clearMetricsButton_->setEnabled(hasSamples);
    }
}

void MainWindow::clearMetricHistory()
{
    metricHistory_.clear();
    updateMetricHistoryUi();
    appendLog(QStringLiteral("Cleared buffered metric history."));
}

void MainWindow::exportMetricHistory()
{
    if (metricHistory_.isEmpty()) {
        appendLog(QStringLiteral("No metric history is available to export."), true);
        return;
    }

    const QString defaultPath = QDir::homePath()
        + QStringLiteral("/NeoLDG-metrics-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
        + QStringLiteral(".csv");

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Metric History"),
        defaultPath,
        QStringLiteral("CSV Files (*.csv)"));

    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendLog(QStringLiteral("Could not write metric history to %1.").arg(filePath), true);
        return;
    }

    QTextStream stream(&file);
    stream << "timestamp_iso,timestamp_ms,forward_watts,reflected_watts,swr,band,band_key\n";

    for (const auto& sample : metricHistory_) {
        stream
            << QDateTime::fromMSecsSinceEpoch(sample.timestampMs).toString(Qt::ISODateWithMs) << ','
            << sample.timestampMs << ','
            << QString::number(sample.forwardWatts, 'f', 3) << ','
            << QString::number(sample.reflectedWatts, 'f', 3) << ','
            << QString::number(sample.swr, 'f', 3) << ','
            << '"' << sample.bandName << '"' << ','
            << sample.bandKey
            << '\n';
    }

    appendLog(QStringLiteral("Exported %1 metric samples to %2.").arg(metricHistory_.size()).arg(filePath));
}

void MainWindow::setTone(QWidget* widget, const QString& tone)
{
    widget->setProperty("tone", tone);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void MainWindow::setConnectionStatus(const QString& text, const QString& tone)
{
    connectionPill_->setText(text);
    setTone(connectionPill_, tone);
}

void MainWindow::setTuneOutcome(neoldg::TuneOutcome outcome, const QString& label)
{
    tunePill_->setText(label.toUpper());

    switch (outcome) {
    case neoldg::TuneOutcome::Good:
        setTone(tunePill_, QStringLiteral("good"));
        break;
    case neoldg::TuneOutcome::Okay:
        setTone(tunePill_, QStringLiteral("warn"));
        break;
    case neoldg::TuneOutcome::Fail:
    case neoldg::TuneOutcome::Error:
        setTone(tunePill_, QStringLiteral("bad"));
        break;
    case neoldg::TuneOutcome::InProgress:
        setTone(tunePill_, QStringLiteral("busy"));
        break;
    case neoldg::TuneOutcome::Idle:
    default:
        setTone(tunePill_, QStringLiteral("idle"));
        break;
    }
}

void MainWindow::appendLog(const QString& message, bool error)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    auto* item = new QListWidgetItem(QStringLiteral("[%1] %2").arg(timestamp, message));
    if (error) {
        item->setForeground(QColor(QStringLiteral("#FFB5A8")));
    }

    logList_->addItem(item);
    while (logList_->count() > 200) {
        delete logList_->takeItem(0);
    }
    logList_->scrollToBottom();
}

double MainWindow::selectedSupplyVoltage() const
{
    return voltageCombo_->currentData().toDouble();
}

double MainWindow::selectedPeakHoldSeconds() const
{
    return peakHoldSpin_ != nullptr ? peakHoldSpin_->value() : 0.25;
}

neoldg::TunerModel MainWindow::selectedTunerModel() const
{
    if (modelCombo_ == nullptr || modelCombo_->currentIndex() < 0) {
        return neoldg::TunerModel::Ldg1000ProII;
    }

    return static_cast<neoldg::TunerModel>(modelCombo_->currentData().toInt());
}

neoldg::MeterSample MainWindow::stabilizedMeterSample(const neoldg::MeterSample& sample)
{
    neoldg::MeterSample stabilized = sample;

    if (neoldg::isKnownBandKey(sample.bandKey)) {
        lastKnownBandKey_ = sample.bandKey;
        hasLastKnownBandKey_ = true;
        return stabilized;
    }

    if (hasLastKnownBandKey_) {
        stabilized.bandKey = lastKnownBandKey_;
    }

    return stabilized;
}

void MainWindow::updateBypassButtonText()
{
    bypassButton_->setText(bypassActive_ ? QStringLiteral("Return To Auto") : QStringLiteral("Enable Bypass"));
}

void MainWindow::onMeterSampleUpdated(const neoldg::MeterSample& sample)
{
    liveSample_ = sample;
    liveTelemetry_ = neoldg::computeDisplayTelemetry(
        stabilizedMeterSample(sample),
        selectedSupplyVoltage(),
        selectedTunerModel());
    recordMetricHistorySample();
    updateMetricHistoryUi();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 holdMs = static_cast<qint64>(selectedPeakHoldSeconds() * 1000.0);

    if (peakHoldCheck_->isChecked()) {
        if (liveTelemetry_.forwardWatts >= displayedForward_) {
            forwardHoldUntilMs_ = now + holdMs;
        }
        if (liveTelemetry_.reflectedWatts >= displayedReflected_) {
            reflectedHoldUntilMs_ = now + holdMs;
        }
        if (liveTelemetry_.swr >= displayedSwr_) {
            swrHoldUntilMs_ = now + holdMs;
        }
    } else {
        forwardHoldUntilMs_ = now;
        reflectedHoldUntilMs_ = now;
        swrHoldUntilMs_ = now;
    }

    animateTelemetry();
}

void MainWindow::onCommandCompleted(const neoldg::CommandResult& result)
{
    appendLog(result.summary, !result.success);

    switch (result.kind) {
    case neoldg::ResponseKind::ToggleAntenna:
        if (result.success) {
            antennaValue_->setText(QStringLiteral("Antenna %1").arg(result.code));
        }
        break;
    case neoldg::ResponseKind::Bypass:
        if (result.success) {
            bypassActive_ = true;
            modeValue_->setText(QStringLiteral("Bypass"));
        }
        break;
    case neoldg::ResponseKind::AutoTune:
        if (result.success) {
            bypassActive_ = false;
            modeValue_->setText(QStringLiteral("Auto"));
        }
        break;
    case neoldg::ResponseKind::ManualTune:
        if (result.success) {
            modeValue_->setText(QStringLiteral("Manual"));
        }
        break;
    case neoldg::ResponseKind::MemoryTune:
    case neoldg::ResponseKind::FullTune:
    case neoldg::ResponseKind::Sync:
    case neoldg::ResponseKind::Unknown:
    default:
        break;
    }

    updateBypassButtonText();
}

QFrame* MainWindow::createCard(const QString& title, QVBoxLayout*& bodyLayout)
{
    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("card"));

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(16);

    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(titleLabel);

    bodyLayout = layout;
    return card;
}

QFrame* MainWindow::createMetricTile(const QString& title, const QString& accent, QLabel*& valueLabel, QProgressBar*& progressBar)
{
    QVBoxLayout* layout = nullptr;
    auto* card = createCard(title, layout);
    card->setMinimumHeight(150);

    valueLabel = new QLabel(QStringLiteral("0.0"));
    valueLabel->setObjectName(QStringLiteral("metricValue"));
    layout->addWidget(valueLabel);

    progressBar = new QProgressBar();
    progressBar->setRange(0, 1000);
    progressBar->setTextVisible(false);
    progressBar->setProperty("tone", accent);
    layout->addWidget(progressBar);

    layout->addStretch(1);
    return card;
}
