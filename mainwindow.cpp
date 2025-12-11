#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCompleter>
#include <QDebug>
#include <QMessageBox>
#include <QDesktopServices>
#include <stdexcept>
#include <QTextStream>
#include <QDate>
#include <QLayoutItem>
#include <QLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    classEntryLayout = new QVBoxLayout(ui->scrollArea->widget());
    ui->scrollArea->widget()->setLayout(classEntryLayout);

    // CREATE ACCOUNT CONNECTIONS
    connect(ui->nextToClassInfoButton, &QPushButton::clicked,
            this, &MainWindow::on_nextToClassInfoButton_clicked);

    connect(ui->finishCreateAccountButton, &QPushButton::clicked,
            this, &MainWindow::on_finishCreateAccountButton_clicked);
    connect(ui->backToLoginButton, &QPushButton::clicked, this, [=]() {
        ui->stackedWidget->setCurrentWidget(ui->loginPage);
    });

    // LOAD STATIONS + SET AUTOCOMPLETE
    loadStations();
    loadStationToLines();

    QStringList stationNames;
    for (const Station &s : stationList)
        stationNames.append(s.name);

    QCompleter *completer = new QCompleter(stationNames, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    ui->stationLineEdit->setCompleter(completer);

    // LOAD STUDENTS
    loadStudents();

    // LOGIN BUTTON
    connect(ui->loginButton, &QPushButton::clicked, this, [=]() {

        QString id = ui->idInput->text().trimmed();

        if (id.isEmpty()) {
            ui->loginErrorLabel->setText("Please enter your CUNY ID.");
            return;
        }

        Student* s = findStudentByID(id);

        if (!s) {
            ui->loginErrorLabel->setText("Account not found.");
            return;
        }

        currentStudent = *s;
        loadClassesForStudent();

        ui->stackedWidget->setCurrentWidget(ui->homePage);
    });
    connect(ui->pushButton,&QPushButton::clicked, this, [=](){
        ui->stackedWidget->setCurrentWidget(ui->signInPage);
    });

    // CONTINUE BUTTON FOR COMMUTE RESULTS
    connect(ui->continueButton, &QPushButton::clicked,
            this, &MainWindow::on_continueButton_clicked);

    // NAVIGATION BUTTONS
    connect(ui->backToHomeButton, &QPushButton::clicked, this, [=]() {
        ui->stackedWidget->setCurrentWidget(ui->homePage);
    });

    connect(ui->WloginButton, &QPushButton::clicked, this, [=]() {
        ui->stackedWidget->setCurrentWidget(ui->loginPage);
    });

    connect(ui->WsigninButton, &QPushButton::clicked, this, [=]() {
        ui->stackedWidget->setCurrentWidget(ui->signInPage);
    });

    connect(ui->backToResultButton, &QPushButton::clicked, this, [=]() {
        ui->stackedWidget->setCurrentWidget(ui->resultPage);
    });

    // LOAD ALERTS + TRAVEL TIMES
    loadServiceAlerts();
    loadTravelTimes();

    // SEND EMAIL BUTTON
    connect(ui->sendEmailButton, &QPushButton::clicked,
            this, &MainWindow::on_sendEmailButton_clicked);
}

MainWindow::~MainWindow()
{
    delete ui;
}



//loads station name and station line from stations.json file
void MainWindow::loadStations()
{
    QFile file("stations.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Could not open stations.json";
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject()) {
        qDebug() << "stations.json is not an object!";
        return;
    }

    QJsonArray arr = doc.object().value("stations").toArray();

    stationList.clear();

    for (int i = 0; i < arr.size(); i++) {

        QJsonObject obj = arr.at(i).toObject();

        Station st;
        st.name = obj["Station Name"].toString();

        QJsonArray jsonLines = obj["Train Lines"].toArray();
        for (int j = 0; j < jsonLines.size(); j++) {
            st.lines.push_back( jsonLines.at(j).toString() );
        }

        stationList.push_back(st);
    }

    qDebug() << "Loaded" << stationList.size() << "stations.";
}


void MainWindow::loadStudents()
{
    try {
        // 1. Open the file
        QFile file("students.json");
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("Could not open students.json");
        }

        QByteArray data = file.readAll();
        file.close();

        // 2. Parse JSON
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            throw std::runtime_error(
                QString("JSON parsing error in students.json: %1")
                    .arg(parseError.errorString()).toStdString()
                );
        }

        if (!doc.isArray()) {
            throw std::runtime_error("The root of students.json must be a JSON array.");
        }

        QJsonArray arr = doc.array();
        studentList.clear();

        // 3. Loop through students
        for (int i = 0; i < arr.size(); i++) {

            QJsonObject obj = arr.at(i).toObject();

            if (!obj.contains("name") || !obj.contains("email") || !obj.contains("cuny_id")) {
                throw std::runtime_error(
                    QString("Missing required student fields in entry #%1").arg(i).toStdString()
                    );
            }

            Student st(
                obj["name"].toString(),
                obj["email"].toString(),
                obj["cuny_id"].toString()
                );

            // 4. Load class list
            QJsonArray classesArr = obj["classes"].toArray();

            for (int j = 0; j < classesArr.size(); j++) {
                QJsonObject cObj = classesArr.at(j).toObject();

                if (!cObj.contains("class_name") ||
                    !cObj.contains("class_time") ||
                    !cObj.contains("professor") ||
                    !cObj.contains("prof_email"))
                {
                    throw std::runtime_error(
                        QString("Missing class fields for student #%1, class #%2")
                            .arg(i).arg(j).toStdString()
                        );
                }

                Student::ClassInfo info;
                info.className  = cObj["class_name"].toString();
                info.classTime  = cObj["class_time"].toString();
                info.professor  = cObj["professor"].toString();
                info.profEmail  = cObj["prof_email"].toString();

                st.classes.push_back(info);
            }

            studentList.push_back(st);
        }

        qDebug() << "Loaded" << studentList.size() << " students.";
    }

    catch (const std::exception &ex) {
        QMessageBox::critical(
            this,
            "Student Load Error",
            "An error occurred while loading students.json:\n" +
                QString::fromStdString(ex.what())
            );
    }
}

Student* MainWindow::findStudentByID(const QString &id)
{
    for (Student &s : studentList) {
        if (s.getID() == id)
            return &s;
    }
    return nullptr;
}

void MainWindow::loadClassesForStudent()
{
    ui->selectClassComboBox->clear();

    for (int i = 0; i < currentStudent.classes.size(); i++)
    {
        Student::ClassInfo c = currentStudent.classes[i];
        QString itemText = c.className + " – " + c.classTime;
        ui->selectClassComboBox->addItem(itemText);
    }
}




void MainWindow::on_continueButton_clicked()
{
    // 1. READ SELECTED STATION
    selectedStation = ui->stationLineEdit->text().trimmed();
    if (selectedStation.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a station.");
        return;
    }

    // 2. READ SELECTED CLASS
    selectedClassText = ui->selectClassComboBox->currentText().trimmed();
    if (selectedClassText.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a class.");
        return;
    }

    // 3. FLEXIBLE TIME PARSER FOR ALL CLASSES
    QStringList parts = selectedClassText.split(" – ");
    if (parts.size() < 2) {
        QMessageBox::warning(this, "Error", "Invalid class time format.");
        return;
    }

    QString fullTimeStr = parts[1].trimmed();

    QStringList timeParts = fullTimeStr.split(QRegularExpression("\\s*-\\s*"));
    if (timeParts.size() < 1) {
        QMessageBox::warning(this, "Error", "Invalid class time format.");
        return;
    }

    QString startTimeStr = timeParts.first().trimmed();


    classStartTime = QTime::fromString(startTimeStr, "h:mm AP");
    if (!classStartTime.isValid())
        classStartTime = QTime::fromString(startTimeStr, "hh:mm AP");

    if (!classStartTime.isValid()) {
        QMessageBox::warning(this, "Error", "Could not read class start time.");
        return;
    }


    // 4. FIND MAIN LINE FOR THE SELECTED STATION
    for (int i = 0; i < stationList.size(); i++) {
        if (stationList[i].name == selectedStation) {
            if (!stationList[i].lines.empty()) {
                mainLine = stationList[i].lines[0].trimmed().toUpper();
            }
            break;
        }
    }

    if (mainLine.isEmpty()) {
        QMessageBox::warning(this, "Error", "Could not determine train line.");
        return;
    }


    // 5. TRAVEL TIME (from travel_times.json)
    int minutes = 30; // default if not found

    for (int i = 0; i < travelTimes.size(); i++){
        if (travelTimes[i].stationName == selectedStation &&
            travelTimes[i].lineName == mainLine)
        {
            minutes = travelTimes[i].minutes;
            break;
        }
    }

    baseTravelMinutes = minutes;
    finalTravelMinutes = baseTravelMinutes + delayPenalty;


    // 6. SERVICE STATUS
    QString status = "GOOD SERVICE"; // default if not found

    for (int i = 0; i < serviceAlertsList.size(); i++)
    {
        if (serviceAlertsList[i].lineName == mainLine)
        {
            status = serviceAlertsList[i].status;
            break;
        }
    }

    // 7. ARRIVAL TIME
    //arrivalTime = QTime::currentTime().addSecs(finalTravelMinutes * 60); (if you want to use current time)
    QTime userStartTime = ui->startTimeEdit->time();
    arrivalTime = userStartTime.addSecs(finalTravelMinutes * 60);
    int diff = classStartTime.secsTo(arrivalTime) / 60;
    if (arrivalTime > classStartTime) {
        willBeLate = true;
        minutesLateOrEarly = diff;
    } else {
        willBeLate = false;
        minutesLateOrEarly = -diff;
    }

    // 8. GET PROFESSOR INFO
    QString profName;

    for (const Student::ClassInfo &c : currentStudent.classes) {
        if (selectedClassText.contains(c.className)) {
            profName = c.professor;
            profEmail = c.profEmail;
            break;
        }
    }

    // 9. BUILD EMAIL BODY
    if (willBeLate) {

        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(
            this,
            "Late Notice",
            "You may be " + QString::number(minutesLateOrEarly) +
                " minutes late. Would you like to notify your professor?",
            QMessageBox::Yes | QMessageBox::No
            );

        if (reply == QMessageBox::Yes) {

            // Build email body
            emailBodyText =
                "Hello " + profName + ",\n\n"
                                      "I may arrive a few minutes late to class today due to subway delays.\n"
                                      "Based on my commute, I might be about " +
                QString::number(minutesLateOrEarly) +
                " minutes late.\n\n"
                "Thank you for your understanding.\n\n"
                "Best regards,\n" +
                currentStudent.getName();

            // Update email preview page UI
            ui->emailBodyTextEdit->setText(emailBodyText);

            // Move to email page
            ui->stackedWidget->setCurrentWidget(ui->emailPreviewPage);
            return;
        }
    }
    else {
        emailBodyText.clear();
    }


    // 10. UPDATE RESULT PAGE UI
    ui->stationResultLabel->setText(selectedStation);
    ui->classResultLabel->setText(selectedClassText);
    ui->serviceStatusLabel->setText(mainLine + " Line – " + status);
    ui->arrivalTimeLabel->setText(arrivalTime.toString("hh:mm AP"));

    if (willBeLate) {
        ui->arrivalStatusLabel->setText(
            "You may be " + QString::number(minutesLateOrEarly) + " minutes late.");
    }
    else if (minutesLateOrEarly > 0) {
        ui->arrivalStatusLabel->setText(
            "You will be " + QString::number(minutesLateOrEarly) + " minutes early.");
    }
    else {
        ui->arrivalStatusLabel->setText("You will arrive on time.");
    }


    // 11. ALTERNATIVE ROUTES (FROM station_to_lines.json)
    ui->alternativeRoutesListWidget->clear();

    if (status != "GOOD SERVICE")
    {
        std::vector<QString> allLines;

        // Find matching station
        for (int i = 0; i < stationToLinesList.size(); i++)
        {
            if (stationToLinesList[i].stationName.compare(selectedStation, Qt::CaseInsensitive) == 0)
            {
                allLines = stationToLinesList[i].lines;
                break;
            }
        }

        // Remove the main line from alternatives
        for (int i = 0; i < allLines.size(); i++)
        {
            if (allLines[i] == mainLine)
            {
                allLines.erase(allLines.begin() + i);
                break;
            }
        }

        // Show alternatives
        for (int i = 0; i < allLines.size(); i++)
        {
            ui->alternativeRoutesListWidget->addItem("Take " + allLines[i] + " Train instead");
        }

        ui->alternativeGroupBox->setVisible(allLines.size() > 0);
    }
    else
    {
        ui->alternativeGroupBox->hide();
    }
    ui->stackedWidget->setCurrentWidget(ui->resultPage);
}

void MainWindow::loadTravelTimes()
{
    QFile file("travel_times.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Could not open travel_times.json";
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();

    travelTimes.clear();

    QList<QString> stationNames = root.keys();

    for (int i = 0; i < stationNames.size(); i++)
    {
        QString station = stationNames[i];
        QJsonObject lineObject = root[station].toObject();
        QList<QString> lineNames = lineObject.keys();
        for (int j = 0; j < lineNames.size(); j++)
        {
            QString line = lineNames[j];
            int minutes = lineObject[line].toInt();
            TravelTime t;
            t.stationName = station;
            t.lineName = line;
            t.minutes = minutes;
            travelTimes.push_back(t);
        }
    }

    qDebug() << "Loaded" << travelTimes.size() << " travel time entries.";
}


void MainWindow::loadServiceAlerts()
{
    QFile file("alerts.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Could not open alerts.json";
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();

    serviceAlertsList.clear();
    QList<QString> lineNames = root.keys();

    for (int i = 0; i < lineNames.size(); i++)
    {
        QString line = lineNames[i];
        QString status = root[line].toString();

        ServiceAlert alert;
        alert.lineName = line;
        alert.status = status;

        serviceAlertsList.push_back(alert);
    }

    qDebug() << "Loaded" << serviceAlertsList.size() << " service alerts.";
}

void MainWindow::loadStationToLines()
{
    QFile file("station_to_lines.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Could not open station_to_lines.json";
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();

    stationToLinesList.clear();
    QList<QString> stationNames = root.keys();

    for (int i = 0; i < stationNames.size(); i++)
    {
        QString station = stationNames[i];
        QJsonArray linesArray = root[station].toArray();

        StationLines entry;
        entry.stationName = station;

        for (int j = 0; j < linesArray.size(); j++)
        {
            QString lineName = linesArray[j].toString();
            entry.lines.push_back(lineName);
        }

        stationToLinesList.push_back(entry);
    }

    qDebug() << "Loaded" << stationToLinesList.size() << " station line groups.";
}

void MainWindow::on_sendEmailButton_clicked()
{
    QString mailTo = "mailto:" + profEmail
                     + "?subject=Late%20Notice"
                     + "&body=" + QUrl::toPercentEncoding(emailBodyText);

    QDesktopServices::openUrl(QUrl(mailTo));
}

void MainWindow::on_nextToClassInfoButton_clicked()
{
    classesToEnter = ui->classCountSpinBox->value();

    // Clear previous dynamic widgets
    QLayoutItem *child;
    while ((child = classEntryLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    // Create input blocks dynamically
    for (int i = 1; i <= classesToEnter; i++) {

        QLabel *title = new QLabel("Class " + QString::number(i) + ":");
        title->setStyleSheet("font-weight: bold; font-size: 16px;");

        QLineEdit *nameEdit = new QLineEdit();
        nameEdit->setPlaceholderText("Class Name");
        nameEdit->setObjectName("classNameEdit_" + QString::number(i));

        QLineEdit *timeEdit = new QLineEdit();
        timeEdit->setPlaceholderText("Class Time (e.g. 10:00 AM - 11:40 AM)");
        timeEdit->setObjectName("classTimeEdit_" + QString::number(i));

        QLineEdit *profEdit = new QLineEdit();
        profEdit->setPlaceholderText("Professor Name");
        profEdit->setObjectName("profNameEdit_" + QString::number(i));

        QLineEdit *emailEdit = new QLineEdit();
        emailEdit->setPlaceholderText("Professor Email");
        emailEdit->setObjectName("profEmailEdit_" + QString::number(i));

        classEntryLayout->addWidget(title);
        classEntryLayout->addWidget(nameEdit);
        classEntryLayout->addWidget(timeEdit);
        classEntryLayout->addWidget(profEdit);
        classEntryLayout->addWidget(emailEdit);
    }

    ui->stackedWidget->setCurrentWidget(ui->classEntryPage);
}

void MainWindow::on_finishCreateAccountButton_clicked()
{
    QString name  = ui->nameLineEdit->text().trimmed();
    QString email = ui->emailLineEdit->text().trimmed();
    QString id    = ui->idLineEdit->text().trimmed();

    Student newStudent(name, email, id);

    for (int i = 1; i <= classesToEnter; i++) {

        QString nameObj  = "classNameEdit_" + QString::number(i);
        QString timeObj  = "classTimeEdit_" + QString::number(i);
        QString profObj  = "profNameEdit_" + QString::number(i);
        QString emailObj = "profEmailEdit_" + QString::number(i);

        QLineEdit *classNameEdit  = ui->classEntryPage->findChild<QLineEdit*>(nameObj);
        QLineEdit *classTimeEdit  = ui->classEntryPage->findChild<QLineEdit*>(timeObj);
        QLineEdit *profNameEdit   = ui->classEntryPage->findChild<QLineEdit*>(profObj);
        QLineEdit *profEmailEdit  = ui->classEntryPage->findChild<QLineEdit*>(emailObj);

        Student::ClassInfo info;
        info.className = classNameEdit->text().trimmed();
        info.classTime = classTimeEdit->text().trimmed();
        info.professor = profNameEdit->text().trimmed();
        info.profEmail = profEmailEdit->text().trimmed();

        newStudent.classes.push_back(info);
    }

    studentList.push_back(newStudent);
    saveStudents();

    QMessageBox::information(this, "Success", "Account created successfully!");
    ui->stackedWidget->setCurrentWidget(ui->loginPage);
}


void MainWindow::saveStudents()
{
    QJsonArray arr;

    for (const Student &s : studentList)
    {
        QJsonObject obj;
        obj["name"]    = s.getName();
        obj["email"]   = s.getEmail();
        obj["cuny_id"] = s.getID();

        QJsonArray classesArr;

        for (const Student::ClassInfo &c : s.classes)
        {
            QJsonObject cObj;
            cObj["class_name"] = c.className;
            cObj["class_time"] = c.classTime;
            cObj["professor"]  = c.professor;
            cObj["prof_email"] = c.profEmail;

            classesArr.append(cObj);
        }

        obj["classes"] = classesArr;
        arr.append(obj);
    }

    QFile file("students.json");
    if (!file.open(QIODevice::WriteOnly))
        return;

    QJsonDocument doc(arr);
    file.write(doc.toJson());
    file.close();
}
