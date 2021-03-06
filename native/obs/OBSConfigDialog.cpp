/*
 * Copyright (c) 2018-present, Frederick Emmott.
 * All rights reserved.
 *
 * This source code is licensed under the MIT license found in the LICENSE file
 * in the root directory of this source tree.
 */

#include "OBSConfigDialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QtGlobal>

#include <limits>

OBSConfigDialog::OBSConfigDialog(const Config& config, QWidget* parent)
  : mConfig(config), QDialog(parent) {
  setWindowTitle(tr("Streaming Remote Settings"));
  setAttribute(Qt::WA_DeleteOnClose, true);
  setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

  auto passwordLabel = new QLabel(tr("Password"), this);
  auto password
    = new QLineEdit(QString::fromUtf8(config.password.c_str()), this);
  password->setMinimumWidth(2 * password->sizeHint().width());
  password->setEchoMode(QLineEdit::Password);
  auto passwordShowHide = new QPushButton(tr("Show"), this);

  const uint16_t maxPort = std::numeric_limits<uint16_t>::max();

  auto tcpPortLabel = new QLabel(tr("TCP Port"), this);
  auto tcpPort = new QSpinBox(this);
  tcpPort->setMinimum(0);
  tcpPort->setMaximum(maxPort);
  tcpPort->setValue(config.tcpPort);

  auto webSocketPortLabel = new QLabel(tr("WebSocket Port"), this);
  auto webSocketPort = new QSpinBox(this);
  webSocketPort->setMinimum(0);
  webSocketPort->setMaximum(maxPort);
  webSocketPort->setValue(config.webSocketPort);

  auto buttonBox = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);

  auto layout = new QGridLayout(this);
  layout->addWidget(passwordLabel, 0, 0);
  layout->addWidget(password, 0, 1);
  layout->addWidget(passwordShowHide, 0, 2);
  layout->addWidget(tcpPortLabel, 1, 0);
  layout->addWidget(tcpPort, 1, 1, 1, 2);
  layout->addWidget(webSocketPortLabel, 2, 0);
  layout->addWidget(webSocketPort, 2, 1, 1, 2);
  layout->addWidget(buttonBox, 3, 0, 1, 3);

  layout->setColumnStretch(1, 1);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  connect(passwordShowHide, &QPushButton::clicked, [=]() {
    if (password->echoMode() == QLineEdit::Password) {
      password->setEchoMode(QLineEdit::Normal);
      passwordShowHide->setText(tr("Hide"));
    } else {
      password->setEchoMode(QLineEdit::Password);
      passwordShowHide->setText(tr("Show"));
    }
  });

  connect(password, &QLineEdit::textChanged, [this](const QString& str) {
    this->mConfig.password = str.toUtf8().toStdString();
  });
  connect(
    tcpPort, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [this](int val) { this->mConfig.tcpPort = val; });
  connect(
    webSocketPort,
    static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
    [this](int val) { this->mConfig.webSocketPort = val; });

  connect(this, &OBSConfigDialog::accepted, [this]() {
    emit configChanged(this->mConfig);
  });
}
