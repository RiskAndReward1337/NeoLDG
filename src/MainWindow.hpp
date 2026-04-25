#pragma once

#include "LdgProtocol.hpp"

#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QtCore/QVector>
#include <QtWidgets/QMainWindow>

class LdgTunerController;
class QBoxLayout;
class QLabel;
class QComboBox;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QDoubleSpinBox;
class QListWidget;
class QFrame;
class QVBoxLayout;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class LayoutMode {
        Full,
        Minimal
    };

    struct MetricHistorySample {
        qint64 timestampMs = 0;
        double forwardWatts = 0.0;
        double reflectedWatts = 0.0;
        double swr = 1.0;
        quint16 bandKey = 0;
        QString bandName;
    };

    void buildUi();
    void applyTheme();
    void bindSignals();
    void refreshPorts();
    void restoreSettings();
    void saveSettings();
    void applyLayoutMode(LayoutMode mode);
    void animateTelemetry();
    void updateConnectionUi(bool connected);
    void updateActionButtons();
    void refreshTelemetryDisplay();
    void setTone(QWidget* widget, const QString& tone);
    void setConnectionStatus(const QString& text, const QString& tone);
    void setTuneOutcome(neoldg::TuneOutcome outcome, const QString& label);
    void appendLog(const QString& message, bool error = false);
    void recomputeTelemetryForSettings();
    void recordMetricHistorySample();
    void updateMetricHistoryUi();
    void clearMetricHistory();
    void exportMetricHistory();
    double selectedSupplyVoltage() const;
    double selectedPeakHoldSeconds() const;
    neoldg::TunerModel selectedTunerModel() const;
    neoldg::MeterSample stabilizedMeterSample(const neoldg::MeterSample& sample);
    void updateBypassButtonText();
    void onMeterSampleUpdated(const neoldg::MeterSample& sample);
    void onCommandCompleted(const neoldg::CommandResult& result);

    QFrame* createCard(const QString& title, QVBoxLayout*& bodyLayout);
    QFrame* createMetricTile(const QString& title, const QString& accent, QLabel*& valueLabel, QProgressBar*& progressBar);

    LdgTunerController* controller_ = nullptr;
    QSettings settings_;
    QTimer* peakDecayTimer_ = nullptr;

    QComboBox* portCombo_ = nullptr;
    QCheckBox* autoConnectCheck_ = nullptr;
    QComboBox* modelCombo_ = nullptr;
    QComboBox* voltageCombo_ = nullptr;
    QCheckBox* peakHoldCheck_ = nullptr;
    QDoubleSpinBox* peakHoldSpin_ = nullptr;

    QPushButton* layoutToggleButton_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* memoryTuneButton_ = nullptr;
    QPushButton* fullTuneButton_ = nullptr;
    QPushButton* antennaButton_ = nullptr;
    QPushButton* bypassButton_ = nullptr;
    QPushButton* autoModeButton_ = nullptr;
    QPushButton* manualModeButton_ = nullptr;
    QPushButton* exportMetricsButton_ = nullptr;
    QPushButton* clearMetricsButton_ = nullptr;

    QLabel* connectionPill_ = nullptr;
    QLabel* tunePill_ = nullptr;
    QLabel* bandValue_ = nullptr;
    QLabel* rawKeyValue_ = nullptr;
    QLabel* connectionValue_ = nullptr;
    QLabel* antennaValue_ = nullptr;
    QLabel* modeValue_ = nullptr;
    QLabel* fwdValue_ = nullptr;
    QLabel* refValue_ = nullptr;
    QLabel* swrValue_ = nullptr;
    QLabel* rawFwdValue_ = nullptr;
    QLabel* rawRefValue_ = nullptr;
    QLabel* metricsCountValue_ = nullptr;

    QProgressBar* fwdBar_ = nullptr;
    QProgressBar* refBar_ = nullptr;
    QProgressBar* swrBar_ = nullptr;

    QBoxLayout* mainLayout_ = nullptr;
    QVBoxLayout* leftColumnLayout_ = nullptr;
    QVBoxLayout* rightColumnLayout_ = nullptr;
    QFrame* heroCard_ = nullptr;
    QFrame* logCard_ = nullptr;
    QFrame* connectionCard_ = nullptr;
    QFrame* controlsCard_ = nullptr;
    QFrame* settingsCard_ = nullptr;
    QFrame* metricsCaptureCard_ = nullptr;
    QWidget* metricsRow_ = nullptr;
    QWidget* rightColumnSpacer_ = nullptr;

    QListWidget* logList_ = nullptr;

    neoldg::MeterSample liveSample_;
    neoldg::DisplayTelemetry liveTelemetry_;
    QVector<MetricHistorySample> metricHistory_;
    LayoutMode layoutMode_ = LayoutMode::Full;
    qint64 lastAnimationTickMs_ = 0;
    qint64 forwardHoldUntilMs_ = 0;
    qint64 reflectedHoldUntilMs_ = 0;
    qint64 swrHoldUntilMs_ = 0;
    quint16 lastKnownBandKey_ = 0;
    bool hasLastKnownBandKey_ = false;
    double displayedForward_ = 0.0;
    double displayedReflected_ = 0.0;
    double displayedSwr_ = 1.0;
    bool bypassActive_ = false;
};
