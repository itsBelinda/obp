/**
 * @file        Window.cpp
 * @brief       Window implementation file.
 * @author      Belinda Kneubühler
 * @date        2020-08-18
 * @copyright   GNU General Public License v2.0
 *
 */

#include "Window.h"
#include <qwt/qwt_dial_needle.h>
#include <iostream>
#include <QtCore/QSettings>


/**
 * The constructor of the Window Class.
 *
 * Needs a Processing object as an argument. It is used to send user input to the Processing Class.
 * Additionally, a QWidget can be passed as an argument to act as the classes parent widget.
 * @param process The Processing class to handle the user inputs.
 * @param parent  The QWidget that is the parent (default 0).
 */
Window::Window(Processing *process, QWidget *parent) :
        dataLength(MAX_DATA_LENGTH),
        process(process),
        QMainWindow(parent)
{

    for (int i = 0; i < MAX_DATA_LENGTH; i++)
    {
        xData[i] = (double) (MAX_DATA_LENGTH - i) / process->getSamplingRate();
        yLPData[i] = 0;
        yHPData[i] = 0;
    }

    std::lock_guard<std::mutex> guard(mtxPlt);
    currentScreen = Screen::startScreen;
    setupUi(this);

    // Generate timer event every to update the window
    (void) startTimer(SCREEN_UPDATE_MS);
}

/**
 * The destructor of the Window class.
 *
 * Stops the process thread if the window is closed.
 */
Window::~Window()
{
    PLOG_VERBOSE << "Cleanup:";
    process->stopThread();
    process->join();
    PLOG_VERBOSE << "Application terminated.";
}

/**
 * This method is to be called at the initialisation of the object. It builds the whole user interface.
 * @param window  A reference to the parent object to set for all initialised objects.
 */
void Window::setupUi(QMainWindow *window)
{
    if (window->objectName().isEmpty())
        window->setObjectName(QString::fromUtf8("MainWindow"));

    QIcon icon(QIcon::fromTheme(QString::fromUtf8("Main")));
    window->resize(1920, 1080);
    window->setAcceptDrops(true);
    window->setWindowIcon(icon);
    window->setTabShape(QTabWidget::Triangular);

    splitter = new QSplitter(window);
    splitter->setObjectName(QString::fromUtf8("splitter"));
    splitter->setOrientation(Qt::Horizontal);
    splitter->setHandleWidth(5);
    splitter->setChildrenCollapsible(false);

    /**
     * The left side is a stacked widget with several pages in a vertical box
     * layout with some permanent information at the top.
     */
    lWidget = new QWidget(splitter);
    vlLeft = new QVBoxLayout(lWidget);
    vlLeft->setObjectName(QString::fromUtf8("vlLeft"));
    lInstructions = new QStackedWidget(lWidget);
    lInstructions->setMinimumWidth(500);

    btnCancel = new QPushButton(lWidget);
    btnCancel->setObjectName(QString::fromUtf8("btnCancel"));

    lMeter = new QLabel(lWidget);
    lMeter->setObjectName(QString::fromUtf8("lMeter"));
    lMeter->setAlignment(Qt::AlignCenter);
    meter = new QwtDial(lWidget);
    meter->setObjectName(QString::fromUtf8("meter"));
    meter->setUpperBound(250.0);
    meter->setScaleStepSize(20.0);
    meter->setWrapping(false);
    meter->setInvertedControls(false);
    meter->setLineWidth(4);
    meter->setMode(QwtDial::RotateNeedle);
    meter->setMinScaleArc(30.0);
    meter->setMaxScaleArc(330.0);
    meter->setMinimumSize(400, 400);
    needle = new QwtDialSimpleNeedle(
            QwtDialSimpleNeedle::Arrow, true, Qt::black,
            QColor(Qt::gray).lighter(130));
    meter->setNeedle(needle);

    // Build pages and add them to the instructions panel
    lInstructions->addWidget(setupStartPage(lInstructions));
    lInstructions->addWidget(setupInflatePage(lInstructions));
    lInstructions->addWidget(setupDeflatePage(lInstructions));
    lInstructions->addWidget(setupEmptyCuffPage(lInstructions));
    lInstructions->addWidget(setupResultPage(lInstructions));

    // Add the instructions panel to the splitter
    vlLeft->addWidget(lMeter);
    vlLeft->addWidget(meter);
    vlLeft->addWidget(lInstructions);
    vlLeft->addWidget(btnCancel);
    btnCancel->hide();
    splitter->addWidget(lWidget);

    // Build and add the plot panel to the splitter
    splitter->addWidget(setupPlots(splitter));
    // Set stretch factor of left part to zero so it will not resize
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // Add splitter to main window.
    window->setCentralWidget(splitter);

    window->setMenuBar(setupMenu(window));

    statusbar = new QStatusBar(window);
    statusbar->setObjectName(QString::fromUtf8("statusbar"));
    window->setStatusBar(statusbar);

    // Settings Dialog
    setupSettingsDialog(window);

    // Info Dialogue
    setupInfoDialogue(window);

    // Set all text fields in one place.
    retranslateUi(window);

    // Connect UI events (slots)
    QMetaObject::connectSlotsByName(window);

    // Set default look, specify percentage of left side:
    double leftSide = 0.3;
    QList<int> Sizes({(int) (leftSide * width()), (int) ((1.0 - leftSide) * width())});
    splitter->setSizes(Sizes);

    connect(btnStart, SIGNAL (released()), this, SLOT (clkBtnStart()));
    connect(btnCancel, SIGNAL (released()), this, SLOT (clkBtnCancel()));
    connect(btnReset, SIGNAL (released()), this, SLOT (clkBtnReset()));

    connect(settingsDialog, SIGNAL(accepted()), this, SLOT(updateValues()));
    connect(settingsDialog, SIGNAL(resetValues()), this, SLOT(resetValuesPerform()));


}

/**
 * Sets up the menu bar to access Settings, Info and Exit.
 * @param parent A reference to the parent widget to set for this page.
 * @return A reference to the generated menu bar.
 */
QMenuBar *Window::setupMenu(QWidget *parent)
{
    actionSettings = new QAction(parent);
    actionSettings->setObjectName(QString::fromUtf8("actionSettings"));
    actionInfo = new QAction(parent);
    actionInfo->setObjectName(QString::fromUtf8("actionInfo"));
    actionExit = new QAction(parent);
    actionExit->setObjectName(QString::fromUtf8("actionExit"));

    menubar = new QMenuBar(parent);
    menubar->setObjectName(QString::fromUtf8("menubar"));
    menuMenu = new QMenu(menubar);
    menuMenu->setObjectName(QString::fromUtf8("menuMenu"));

    menubar->addAction(menuMenu->menuAction());
    menuMenu->addAction(actionSettings);
    menuMenu->addAction(actionInfo);
    menuMenu->addSeparator();
    menuMenu->addAction(actionExit);
    return menubar;
}

/**
 * Sets up the page with two plots to display the data.
 *
 * Both plots have time as the x-axis.
 * The first plot displays low pass filtered pressure data the y axis is in
 * mmHg pressure.
 * The second plot displays the high pass filtered oscillation data, it is in
 * arbitrary units.
 *
 * @param parent A reference to the parent widget to set for this page.
 * @return A reference to the generated page.
 */
QWidget *Window::setupPlots(QWidget *parent)
{
    rWidget = new QWidget(parent);

    vlRight = new QVBoxLayout();
    vlRight->setObjectName(QString::fromUtf8("vlRight"));

    line = new QFrame(parent);
    line->setObjectName(QString::fromUtf8("line"));
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    /**
     * The axises of the plot are currently fixed and can not be changed.
     */
    pltPre = new Plot(xData, yLPData, dataLength, 250, 0.0, parent);
    pltPre->setObjectName(QString::fromUtf8("pltPre"));
    pltPre->setAxisTitles("time (s)", "pressure (mmHg)");
    pltOsc = new Plot(xData, yHPData, dataLength, 4, -3, parent);
    pltOsc->setObjectName(QString::fromUtf8("pltOsc"));
    pltOsc->setAxisTitles("time (s)", "oscillations (ΔmmHg)");

    // Align y-axises vertically
    double extP = pltPre->getyAxisExtent();
    double extO = pltOsc->getyAxisExtent();
    double extent = (extP > extO) ? extP : extO;
    pltOsc->setyAxisExtent(extent);
    pltPre->setyAxisExtent(extent);

    // build right side of window
    vlRight->addWidget(pltPre);
    vlRight->addWidget(line);
    vlRight->addWidget(pltOsc);
    rWidget->setLayout(vlRight);

    return rWidget;

}

/**
 * Sets up the start page with instructions.
 *
 * A button on the button can start the measurements.
 *
 * @param parent A reference to the parent widget to set for this page.
 * @return A reference to the generated page.
 */
QWidget *Window::setupStartPage(QWidget *parent)
{
    lInstrStart = new QWidget(parent);

    vlStart = new QVBoxLayout();
    vlStart->setObjectName(QString::fromUtf8("vlStart"));

    lInfoStart = new QLabel(parent);
    lInfoStart->setObjectName(QString::fromUtf8("lInfoStart"));
    lInfoStart->setWordWrap(true);
    lInfoStart->setAlignment(Qt::AlignCenter);

    btnStart = new QPushButton(parent);
    btnStart->setObjectName(QString::fromUtf8("btnStart"));
    btnStart->setDisabled(true);

    vSpace4 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    vSpace5 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

    vlStart->addItem(vSpace4);
    vlStart->addWidget(lInfoStart);
    vlStart->addItem(vSpace5);
    vlStart->addWidget(btnStart);

    lInstrStart->setLayout(vlStart);
    return lInstrStart;

}

/**
 * Sets up the inflation page with instructions.
 *
 * Instructs the user to pump up the cuff.
 *
 * @param parent A reference to the parent widget to set for this page.
 * @return A reference to the generated page.
 */
QWidget *Window::setupInflatePage(QWidget *parent)
{

    lInstrPump = new QWidget(parent);

    // Layout for this page:
    vlInflate = new QVBoxLayout();
    vlInflate->setObjectName(QString::fromUtf8("vlInflate"));

    vSpace1 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    vSpace2 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

    lInfoPump = new QLabel(parent);
    lInfoPump->setObjectName(QString::fromUtf8("lInfoPump"));
    lInfoPump->setWordWrap(true);
    lInfoPump->setAlignment(Qt::AlignCenter);

    // build left side of window
    vlInflate->addItem(vSpace1);
    vlInflate->addWidget(lInfoPump);
    vlInflate->addItem(vSpace2);

    lInstrPump->setLayout(vlInflate);
    return lInstrPump;
}

/**
 * Sets up the inflation page with instructions.
 *
 * Instructs the user to pump up the cuff.
 *
 * @param parent A reference to the parent widget to set for this page.
 * @return A reference to the generated page.
 */
QWidget *Window::setupDeflatePage(QWidget *parent)
{
    lInstrRelease = new QWidget(parent);

    vlRelease = new QVBoxLayout();
    lInfoRelease = new QLabel(parent);
    lInfoRelease->setObjectName(QString::fromUtf8("lInfoRelease"));
    lInfoRelease->setWordWrap(true);
    lInfoRelease->setAlignment(Qt::AlignCenter);
    lheartRate = new QLabel(parent);
    lheartRate->setObjectName(QString::fromUtf8("lheartRate"));
    lheartRate->setAlignment(Qt::AlignCenter);

    vSpace4 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    vSpace5 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

    vlRelease->addItem(vSpace5);
    vlRelease->addWidget(lInfoRelease);
    vlRelease->addItem(vSpace4);
    vlRelease->addWidget(lheartRate);

    lInstrRelease->setLayout(vlRelease);
    return lInstrRelease;
}

/**
 * Sets up the empty cup page with instructions.
 *
 * Instructs the user to deflate the cuff completely.
 *
 * @param parent A reference to the parent widget to set for this page.
 * @return A reference to the generated page.
 */
QWidget *Window::setupEmptyCuffPage(QWidget *parent)
{
    lInstrDeflate = new QWidget(parent);

    vlDeflate = new QVBoxLayout();
    lInfoDeflate = new QLabel(parent);
    lInfoDeflate->setObjectName(QString::fromUtf8("lInfoDeflate"));
    lInfoDeflate->setWordWrap(true);
    lInfoDeflate->setAlignment(Qt::AlignCenter);

    vlDeflate->addWidget(lInfoDeflate);
    lInstrDeflate->setLayout(vlDeflate);
    return lInstrDeflate;
}

/**
 * Sets up the results page.
 *
 * Displays the calculated results.
 *
 * @param parent A reference to the parent widget to set for this page.
 * @return A reference to the generated page.
 */
QWidget *Window::setupResultPage(QWidget *parent)
{
    lInstrResult = new QWidget(parent);

    vlResult = new QVBoxLayout();
    lInfoResult = new QLabel(parent);
    lInfoResult->setObjectName(QString::fromUtf8("lInfoDeflate"));
    lInfoResult->setWordWrap(true);
    lInfoResult->setAlignment(Qt::AlignCenter);
    vSpace6 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    vSpace7 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

    btnReset = new QPushButton(parent);
    btnReset->setObjectName(QString::fromUtf8("btnReset"));
    QFont flb;
    flb.setBold(true);
    flb.setPointSize(flb.pointSize() + 4);
    QFont fb;
    fb.setBold(true);
    flb.setPointSize(flb.pointSize() + 2);

    flResults = new QFormLayout();
    flResults->setObjectName(QString::fromUtf8("flResults"));
    lMeasured = new QLabel(parent);
    lMeasured->setObjectName(QString::fromUtf8("lMeasured"));
    lEstimated = new QLabel(parent);
    lEstimated->setObjectName(QString::fromUtf8("lEstimated"));
    lMAP = new QLabel(parent);
    lMAP->setFont(flb);
    lMAP->setObjectName(QString::fromUtf8("lMAP"));
    lMAPval = new QLabel(parent);
    lMAPval->setObjectName(QString::fromUtf8("lMAPval"));
    lMAPval->setFont(flb);
    lSBP = new QLabel(parent);
    lSBP->setObjectName(QString::fromUtf8("lSBP"));
    lSBP->setFont(fb);
    lSBPval = new QLabel(parent);
    lSBPval->setObjectName(QString::fromUtf8("lSBPval"));
    lSBPval->setFont(fb);
    lDBP = new QLabel(parent);
    lDBP->setObjectName(QString::fromUtf8("lDBP"));
    lDBP->setFont(fb);
    lDBPval = new QLabel(parent);
    lDBPval->setObjectName(QString::fromUtf8("lDBPval"));
    lDBPval->setFont(fb);
    lheartRateAV = new QLabel(parent);
    lheartRateAV->setObjectName(QString::fromUtf8("lheartRateAV"));
    flb.setBold(false);
    lheartRateAV->setFont(fb);
    lheartRateAV->setMinimumWidth(150);
    lHRvalAV = new QLabel(parent);
    lHRvalAV->setObjectName(QString::fromUtf8("lHRvalAV"));
    lHRvalAV->setFont(fb);
    lHRvalAV->setMinimumWidth(150);
    lMAPval->setAlignment(Qt::AlignRight);
    lHRvalAV->setAlignment(Qt::AlignRight);
    lDBPval->setAlignment(Qt::AlignRight);
    lSBPval->setAlignment(Qt::AlignRight);

    flResults->setWidget(0, QFormLayout::LabelRole, lMeasured);
    flResults->setWidget(1, QFormLayout::LabelRole, lMAP);
    flResults->setWidget(1, QFormLayout::FieldRole, lMAPval);
    flResults->setWidget(3, QFormLayout::LabelRole, lEstimated);
    flResults->setWidget(4, QFormLayout::LabelRole, lSBP);
    flResults->setWidget(4, QFormLayout::FieldRole, lSBPval);
    flResults->setWidget(5, QFormLayout::LabelRole, lDBP);
    flResults->setWidget(5, QFormLayout::FieldRole, lDBPval);
    flResults->setWidget(2, QFormLayout::LabelRole, lheartRateAV);
    flResults->setWidget(2, QFormLayout::FieldRole, lHRvalAV);
    flResults->setContentsMargins(50, 0, 50, 0);

    vlResult->addItem(vSpace6);
    vlResult->addWidget(lInfoResult);
    vlResult->addLayout(flResults);
    vlResult->addItem(vSpace7);
    vlResult->addWidget(btnReset);
    lInstrResult->setLayout(vlResult);

    return lInstrResult;
}

/**
 * Sets up the dialog for the setting.
 *
 * The user can adjust parameters, they will be saved when the application is closed.
 *
 * @param parent A reference to the parent widget to set for this page.
 * @return A reference to the generated page.
 */
QWidget *Window::setupSettingsDialog(QWidget *parent)
{
    settingsDialog = new SettingsDialog(parent);
    loadSettings();
    return settingsDialog;
}

/**
 * Sets up the dialog for the information.
 *
 * @param parent A reference to the parent widget to set for this page.
 * @return A reference to the generated page.
 */
QWidget *Window::setupInfoDialogue(QWidget *parent)
{
    infoDialogue = new InfoDialog(parent);
    return infoDialogue;
}

/**
 * Sets all the text in the main window.
 * @param window A pointer to the object itself.
 */
void Window::retranslateUi(QMainWindow *window)
{
    window->setWindowTitle("Oscillometric Blood Pressure Measurement");

    lInfoStart->setText("<b>Prepare the measurement:</b><br><br>"
                        "1. Put the cuff on the upper arm of your non-dominant hand, making sure it is tight.<br>"
                        "2. Rest your arm on a flat surface.<br>"
                        "3. Take the pump into your dominant hand.<br>"
                        "4. Make sure the valve is closed, but you can handle it easily.<br>"
                        "5. Press Start when you are ready.");
    lInfoPump->setText(QString("<b>Pump-up to %1 mmHg</b><br><br>"
                               "Using your dominant hand, where your arm is not in the cuff, quickly pump up the cuff to %1 mmHg.<br><br>"
                               "The valve should stay fully closed.<br>"
                               "Use the dial above for reference.").arg(pumpUpVal));

    lInfoRelease->setText("<b>Slowly and continuously release pressure.</b><br><br>"
                          "Open the valve slightly to release pressure at approximately 3 mmHg/s.<br>"
                          "Wait calmly and try not to move. <br><br>"
                          "<b>Take your time. The deflation should be as uniform as possible.</b><br><br>");
    lInfoDeflate->setText("<b>Completely open the valve.</b><br><br>"
                          "Wait for the pressure to go down to 0 mmHg.<br><br>"
                          "You will see the results next.");
    lInfoResult->setText("<b>Results:</b><br>");

    lMeter->setText("<b>Pressure in mmHg:</b>");
    btnStart->setText("Start");
    btnReset->setText("Reset");
    btnCancel->setText("Cancel");
    lMeasured->setText("measured:");
    lMAP->setText("<b>MAP:</b>");//<font color="red"></font>
    lMAPval->setText("- mmHg");
    lSBP->setText(QString("<b>SBP (r=%1):</b>").arg(process->getRatioSBP()));
    lSBPval->setText("- mmHg");
    lDBP->setText(QString("<b>DBP (r=%1):</b>").arg(process->getRatioDBP()));
    lDBPval->setText("- mmHg");
    lheartRate->setText("Current heart rate:<br><b>--</b>");
    lheartRateAV->setText("Heart rate:");
    lHRvalAV->setText("- beats/min");
    lEstimated->setText("estimated:");
    pltPre->setPlotTitle("Pressure, low-pass filtered");
    pltOsc->setPlotTitle("Oscillations, high-pass filtered");

    actionSettings->setText("Settings");
    actionInfo->setText("Info");
    actionExit->setText("Exit");
    menuMenu->setTitle("Menu");

}

/**
 * Handles the timer event to update the UI.
 *
 * Always acquires a mutex first, before accessing the plots.
 */
void Window::timerEvent(QTimerEvent *)
{
    mtxPlt.lock();
    pltOsc->replot();
    pltPre->replot();
    mtxPlt.unlock();
}

/**
 * Handles notifications about a new data pair.
 *
 * Always acquires a mutex first, before accessing the plots.
 * Updates the pressure meter through a queued connection event that is called by the Qt thread.
 * @param pData The newly available pressure data.
 * @param oData The newly available oscillation data.
 */
void Window::eNewData(double pData, double oData)
{
    mtxPlt.lock();
    pltPre->setNewData(pData);
    pltOsc->setNewData(oData);
    mtxPlt.unlock();

    bool bOk = QMetaObject::invokeMethod(meter, "setValue", Qt::QueuedConnection, Q_ARG(double, pData));
    assert(bOk);
}

/**
 * Handles notifications to switch the displayed screen.
 * @param eNewScreen The new screen to display.
 */
void Window::eSwitchScreen(Screen eNewScreen)
{
    bool bOk = false;
    switch (eNewScreen)
    {
        case Screen::startScreen:
            bOk = QMetaObject::invokeMethod(btnCancel, "hide", Qt::QueuedConnection);
            assert(bOk);
            bOk = QMetaObject::invokeMethod(lInstructions, "setCurrentIndex", Qt::AutoConnection,
                                            Q_ARG(int, 0));
            assert(bOk);
            break;
        case Screen::inflateScreen:
            bOk = QMetaObject::invokeMethod(btnCancel, "show", Qt::QueuedConnection);
            assert(bOk);
            bOk = QMetaObject::invokeMethod(lInstructions, "setCurrentIndex", Qt::AutoConnection,
                                            Q_ARG(int, 1));
            assert(bOk);
            break;
        case Screen::deflateScreen:
            bOk = QMetaObject::invokeMethod(btnCancel, "show", Qt::QueuedConnection);
            assert(bOk);
            bOk = QMetaObject::invokeMethod(lInstructions, "setCurrentIndex", Qt::AutoConnection,
                                            Q_ARG(int, 2));
            assert(bOk);
            break;
        case Screen::emptyCuffScreen:
            bOk = QMetaObject::invokeMethod(btnCancel, "show", Qt::QueuedConnection);
            assert(bOk);
            bOk = QMetaObject::invokeMethod(lInstructions, "setCurrentIndex", Qt::AutoConnection,
                                            Q_ARG(int, 3));
            assert(bOk);
            break;
        case Screen::resultScreen:
            bOk = QMetaObject::invokeMethod(btnCancel, "hide", Qt::QueuedConnection);
            assert(bOk);
            bOk = QMetaObject::invokeMethod(lInstructions, "setCurrentIndex", Qt::AutoConnection,
                                            Q_ARG(int, 4));
            assert(bOk);
            break;
    }
    currentScreen = eNewScreen;
}

/**
 * Handles notifications for the results.
 * @param map The determined value for mean arterial pressure.
 * @param sbp The determined value for systolic blood pressure.
 * @param dbp The determined value for diastolic blood pressure.
 */
void Window::eResults(double map, double sbp, double dbp)
{
    bool bOk = QMetaObject::invokeMethod(lMAPval, "setText", Qt::QueuedConnection,
                                         Q_ARG(QString, (QString::number(map, 'f', 0) + " mmHg")));
    assert(bOk);
    bOk = QMetaObject::invokeMethod(lSBPval, "setText", Qt::QueuedConnection,
                                    Q_ARG(QString, QString::number(sbp, 'f', 0) + " mmHg"));
    assert(bOk);
    bOk = QMetaObject::invokeMethod(lDBPval, "setText", Qt::QueuedConnection,
                                    Q_ARG(QString, QString::number(dbp, 'f', 0) + " mmHg"));
    assert(bOk);
}

/**
 * Handles notificaions about heart rate and displays them in the appropriate location in the UI.
 * @param heartRate The latest heart rate value. Can either be a current or an average heart rate value.
 */
void Window::eHeartRate(double heartRate)
{

    bool bOk = QMetaObject::invokeMethod(lheartRate, "setText", Qt::QueuedConnection,
                                         Q_ARG(QString, "Current heart rate:<br><b>" +
                                                        QString::number(heartRate, 'f', 0) + "</b>"));
    assert(bOk);
    bOk = QMetaObject::invokeMethod(lHRvalAV, "setText", Qt::QueuedConnection,
                                    Q_ARG(QString, QString::number(heartRate, 'f', 0) + " beats/min"));
    assert(bOk);
}

/**
 * Handles the notification that the observed class is ready.
 */
void Window::eReady()
{
    /**
     * Instead of: <br>
     * btnStart->setDisabled(false);<br>
     * The QMetaObject::invokeMethod is used with a Qt::QueuedConnection.<br>
     * The button is set enabled whenever the UI thread is ready.setDisabled
     */
    bool bOk = QMetaObject::invokeMethod(btnStart, "setDisabled", Qt::QueuedConnection,
                                         Q_ARG(bool, false));
    /**
     * IChecks that function call is valid during development. <br>
     * Do not put function inside assert, because it will be removed in the release build!
     */
    assert(bOk);
}

/**
 * Handles click events on the button 'Start'.
 */
void Window::clkBtnStart()
{
    process->startMeasurement();
}

/**
 * Handles click events on the button 'Cancel'.
 */
void Window::clkBtnCancel()
{
    process->stopMeasurement();
}

/**
 * Handles click events on the button 'Reset'.
 */
void Window::clkBtnReset()
{
    process->stopMeasurement();
}


/**
 * This function is called when the menu entry "Info" is pressed.
 *
 * The name of this function (slot) ensures automatic connection with the menu entry
 * actionInfo.
 */
void Window::on_actionInfo_triggered()
{
    infoDialogue->setModal(true);
    infoDialogue->show();
}

/**
 * This function is called when the menu entry "Settings" is pressed.
 *
 * The name of this function (slot) ensures automatic connection with the menu entry
 * actionSettings.
 */
void Window::on_actionSettings_triggered()
{
    settingsDialog->setModal(true);
    settingsDialog->show();
}

/**
 * This function is called when the menu entry "Exit" is pressed.
 *
 * The name of this function (slot) ensures automatic connection with the menu entry
 * actionExit.
 */
void Window::on_actionExit_triggered()
{
    QApplication::quit();
}

/**
 * loads the settings from the settings file if there is one, otherwise default values are displayed.
 */
void Window::loadSettings()
{
    /** For every value: get the values from settings and get the default value from Processing.
    * Then set both the value in the settings dialog and in Processing with what was stored in the settings.
    * Changing the values in Processing only works at startup, before the process is running.
    */
    int iVal;
    double dVal;
    QSettings settings; //Saves setting platform independent.
    dVal = settings.value("ratioSBP", process->getRatioSBP()).toDouble();
    settingsDialog->setRatioSBP(dVal);
    process->setRatioSBP(dVal);

    dVal = settings.value("ratioDBP", process->getRatioDBP()).toDouble();
    settingsDialog->setRatioDBP(dVal);
    process->setRatioDBP(dVal);

    iVal = settings.value("minNbrPeaks", process->getMinNbrPeaks()).toInt();
    settingsDialog->setMinNbrPeaks(iVal);
    process->setMinNbrPeaks(iVal);

    iVal = settings.value("pumpUpValue", process->getPumpUpValue()).toInt();
    settingsDialog->setPumpUpValue(iVal);
    process->setPumpUpValue(iVal);
    pumpUpVal = iVal;
}

/**
 * Does only update the values in the settings file. They will not be applied until the
 * application is restarted.
 */
void Window::updateValues()
{
    QSettings settings;
    settings.setValue("ratioSBP", settingsDialog->getRatioSBP());
    settings.setValue("ratioDBP", settingsDialog->getRatioDBP());
    settings.setValue("minNbrPeaks", settingsDialog->getMinNbrPeaks());
    settings.setValue("pumpUpValue", settingsDialog->getPumpUpValue());
}

/**
 * Resets all values both in the application and in the settings file.
 * Changes take effect immediately.
 */
void Window::resetValuesPerform()
{
    process->resetConfigValues();
    QSettings settings;
    settings.setValue("ratioSBP", process->getRatioSBP());
    settings.setValue("ratioDBP", process->getRatioDBP());
    settings.setValue("minNbrPeaks", process->getMinNbrPeaks());
    settings.setValue("pumpUpValue", process->getPumpUpValue());
    // Reload the values from settings also writes them to the settings dialog:
    loadSettings();
    retranslateUi(this);
}