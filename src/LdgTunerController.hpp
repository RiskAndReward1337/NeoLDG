#pragma once

#include "LdgProtocol.hpp"

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtSerialPort/QSerialPort>

class LdgTunerController : public QObject
{
    Q_OBJECT

public:
    explicit LdgTunerController(QObject* parent = nullptr);
    ~LdgTunerController() override;

    bool isConnected() const;
    bool isBusy() const;
    QString portName() const;

public slots:
    void connectSerial(const QString& portName);
    void disconnectSerial();
    void toggleAntenna();
    void memoryTune();
    void fullTune();
    void toggleBypass();
    void setAutoTuneMode();
    void setManualTuneMode();

signals:
    void connectionChanged(bool connected, const QString& portName);
    void busyChanged(bool busy);
    void meterSampleUpdated(const neoldg::MeterSample& sample);
    void tuneOutcomeChanged(neoldg::TuneOutcome outcome, const QString& label);
    void commandCompleted(const neoldg::CommandResult& result);
    void statusMessage(const QString& message, bool error);
    void logMessage(const QString& message);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);
    void onControlSettled();
    void onResponseTimeout();
    void onSilentReconnectTimeout();

private:
    enum class RxMode {
        Meter,
        ControlSettling,
        AwaitingResponse
    };

    bool openSerialPort(const QString& portName, bool announceConnection, bool announceFailure, bool emitConnectionSignal);
    void setBusy(bool busy);
    void emitStatus(const QString& message, bool error = false);
    void resetMeterParser();
    void beginSilentMeterReconnect();
    void processMeterByte(char byte);
    void sendWakeAndCommand(char command);
    void startCommand(neoldg::ResponseKind kind, char command, const QString& description);
    void finishCommand(const neoldg::CommandResult& result);
    void returnToMeterMode();
    void resetPendingCommand();

    QSerialPort serial_;
    QTimer controlSettleTimer_;
    QTimer responseTimer_;
    QTimer silentReconnectTimer_;

    QByteArray meterPayload_;
    int eomCount_ = 0;

    RxMode rxMode_ = RxMode::Meter;
    neoldg::ResponseKind pendingKind_ = neoldg::ResponseKind::Unknown;
    char pendingCommand_ = '\0';
    QString pendingDescription_;
    bool busy_ = false;
    bool bypassActive_ = false;
    QString reconnectPortName_;
    bool silentReconnectInProgress_ = false;
    int silentReconnectAttempts_ = 0;
};
