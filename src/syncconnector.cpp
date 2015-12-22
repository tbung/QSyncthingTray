/******************************************************************************
// QSyncthingTray
// Copyright (c) Matthias Frick, All rights reserved.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3.0 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library.
******************************************************************************/

#include "syncconnector.h"
#include <QtGui>
#include <QObject>
#include <QMessageBox>
#include <QStyleFactory>
#include <iostream>

namespace mfk
{
namespace connector
{

  
//------------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------//
SyncConnector::SyncConnector(QUrl url)
{
  mCurrentUrl = url;
  connect(
          &network, SIGNAL (finished(QNetworkReply*)),
          this, SLOT (netRequestfinished(QNetworkReply*))
          );
  connect(
          &network, SIGNAL (sslErrors(QNetworkReply *, QList<QSslError>)),
          this, SLOT (onSslError(QNetworkReply*))
          );
}

  
//------------------------------------------------------------------------------------//

void SyncConnector::setURL(QUrl url, std::string username, std::string password,
  ConnectionStateCallback setText)
{
  mAuthentication = std::make_pair(username, password);
  url.setUserName(mAuthentication.first.c_str());
  url.setPassword(mAuthentication.second.c_str());
  mCurrentUrl = url;
  url.setPath(tr("/rest/system/version"));
  mConnectionStateCallback = setText;
  QNetworkRequest request(url);
  network.clearAccessCache();
  QNetworkReply *reply = network.get(request);
  requestMap[reply] = kRequestMethod::urlTested;
}


//------------------------------------------------------------------------------------//

void SyncConnector::showWebView()
{
  if (mpWebView != nullptr)
  {
    mpWebView->close();
  }
  std::unique_ptr<QWebViewClose> pWeb(new QWebViewClose());
  mpWebView = std::move(pWeb);
  mpWebView->show();
  connect(mpWebView->page()->networkAccessManager(),
    SIGNAL(sslErrors(QNetworkReply*, const QList<QSslError> & )),
    this,
    SLOT(onSslError(QNetworkReply*)));
  mpWebView->load(mCurrentUrl);
  mpWebView->setStyle(QStyleFactory::create("Fusion"));
  sysutils::SystemUtility().showDockIcon(true);
  mpWebView->raise();
}


//------------------------------------------------------------------------------------//

void SyncConnector::urlTested(QNetworkReply* reply)
{
  ignoreSslErrors(reply);

  std::pair<std::string, bool> connectionInfo =
    api::V12API().getConnectionInfo(reply);
  
  int versionNumber = getCurrentVersion(connectionInfo.first);
  mAPIHandler =
    std::unique_ptr<api::APIHandlerBase>(api::V12API().getAPIForVersion(versionNumber));
  if (mConnectionStateCallback != nullptr)
  {
    mConnectionStateCallback(connectionInfo);
  }
  reply->deleteLater();
}


//------------------------------------------------------------------------------------//

void SyncConnector::checkConnectionHealth()
{
  QUrl requestUrl = mCurrentUrl;
  requestUrl.setPath(tr("/rest/system/connections"));
  QNetworkRequest request(requestUrl);

  QNetworkReply *reply = network.get(request);
  requestMap[reply] = kRequestMethod::connectionHealth;
  getCurrentConfig();
}


//------------------------------------------------------------------------------------//

void SyncConnector::getCurrentConfig()
{
  QUrl requestUrl = mCurrentUrl;
  requestUrl.setPath(tr("/rest/system/config"));
  QNetworkRequest request(requestUrl);
  
  QNetworkReply *reply = network.get(request);
  requestMap[reply] = kRequestMethod::getCurrentConfig;
}


//------------------------------------------------------------------------------------//

void SyncConnector::setConnectionHealthCallback(ConnectionHealthCallback cb)
{
  mConnectionHealthCallback = cb;
  if (mpConnectionHealthTimer)
  {
    mpConnectionHealthTimer->stop();
  }
  mpConnectionHealthTimer = std::unique_ptr<QTimer>(new QTimer(this));
  connect(mpConnectionHealthTimer.get(), SIGNAL(timeout()), this,
    SLOT(checkConnectionHealth()));
  mpConnectionHealthTimer->start(3000);
}


//------------------------------------------------------------------------------------//

void SyncConnector::syncThingProcessSpawned(QProcess::ProcessState newState)
{
  if (mProcessSpawnedCallback)
  {
    switch (newState)
    {
      case QProcess::Running:
        mProcessSpawnedCallback(kSyncthingProcessState::SPAWNED);
        break;
      case QProcess::NotRunning:
         mProcessSpawnedCallback(kSyncthingProcessState::NOT_RUNNING);
        break;
      default:
        mProcessSpawnedCallback(kSyncthingProcessState::NOT_RUNNING);
    }
  }
}

 
//------------------------------------------------------------------------------------//

void SyncConnector::setProcessSpawnedCallback(ProcessSpawnedCallback cb)
{
  mProcessSpawnedCallback = cb;
}


//------------------------------------------------------------------------------------//

void SyncConnector::netRequestfinished(QNetworkReply* reply)
{
  switch (requestMap[reply])
  {
    case kRequestMethod::getCurrentConfig:
      currentConfigReceived(reply);
      break;
    case kRequestMethod::connectionHealth:
      connectionHealthReceived(reply);
      break;
    case kRequestMethod::urlTested:
      urlTested(reply);
      break;
  }
  requestMap.remove(reply);
}


//------------------------------------------------------------------------------------//

void SyncConnector::connectionHealthReceived(QNetworkReply* reply)
{
  ignoreSslErrors(reply);
  QByteArray replyData;
  if (reply->error() == QNetworkReply::NoError)
  {
    replyData = reply->readAll();
  }
  std::map<std::string, std::string> result = mAPIHandler->getConnections(replyData);
  if (mConnectionHealthCallback != nullptr)
  {
    mConnectionHealthCallback(result);
  }
  reply->deleteLater();
}


//------------------------------------------------------------------------------------//

void SyncConnector::currentConfigReceived(QNetworkReply *reply)
{
  ignoreSslErrors(reply);
  QByteArray replyData;
  if (reply->error() == QNetworkReply::NoError)
  {
    replyData = reply->readAll();
  }
  mAPIKey = mAPIHandler->getCurrentAPIKey(replyData);
  mFolders = mAPIHandler->getCurrentFolderList(replyData);
  reply->deleteLater();
}


//------------------------------------------------------------------------------------//

void SyncConnector::shutdownSyncthingProcess()
{
  QUrl requestUrl = mCurrentUrl;
  requestUrl.setPath(tr("/rest/system/shutdown"));
  QNetworkRequest request(requestUrl);
  QByteArray postData;
  // Call the webservice
  QNetworkAccessManager *networkManager = new QNetworkAccessManager(this);
  connect(networkManager, SIGNAL(finished(QNetworkReply*)),
          SLOT(shutdownProcessPosted(QNetworkReply*)));
  connect(networkManager, SIGNAL (sslErrors(QNetworkReply *, QList<QSslError>)),
          this, SLOT (onSslError(QNetworkReply*))
          );
  QNetworkRequest networkRequest(requestUrl);
  std::string headerStr = "X-API-KEY: " + mAPIKey;
  QByteArray headerByte(mAPIKey.c_str(), mAPIKey.length());
  networkRequest.setRawHeader("X-API-Key", headerByte);
  
  networkManager->post(networkRequest,postData);
}

//------------------------------------------------------------------------------------//

void SyncConnector::shutdownProcessPosted(QNetworkReply *reply)
{
  if (mProcessSpawnedCallback)
  {
    mProcessSpawnedCallback(kSyncthingProcessState::PAUSED);
  }
  reply->deleteLater();
}


//------------------------------------------------------------------------------------//

void SyncConnector::spawnSyncthingProcess(
  std::string filePath, const bool shouldSpawn, const bool onSetPath)
{
  if (shouldSpawn)
  {
    if (!checkIfFileExists(tr(filePath.c_str())) && onSetPath)
    {
      QMessageBox msgBox;
      msgBox.setText("Could not find Syncthing.");
      msgBox.setInformativeText("Are you sure the path is correct?");
      msgBox.setStandardButtons(QMessageBox::Ok);
      msgBox.setDefaultButton(QMessageBox::Ok);
      msgBox.exec();
    }
    if (!systemUtil.isBinaryRunning(std::string("syncthing")))
    {
      mpSyncProcess = std::unique_ptr<QProcess>(new QProcess(this));
      connect(mpSyncProcess.get(), SIGNAL(stateChanged(QProcess::ProcessState)),
        this, SLOT(syncThingProcessSpawned(QProcess::ProcessState)));
      QString processPath = filePath.c_str();
      QStringList launchArgs;
      launchArgs << "-no-browser";
      mpSyncProcess->start(processPath, launchArgs);
    }
    else
    {
      if (mProcessSpawnedCallback != nullptr)
      {
        mProcessSpawnedCallback(kSyncthingProcessState::ALREADY_RUNNING);
      }
    }
  }
  else
  {
    shutdownSyncthingProcess();
    killProcesses();
  }
}


//------------------------------------------------------------------------------------//

std::list<std::pair<std::string, std::string>> SyncConnector::getFolders()
{
  return mFolders;
}


//------------------------------------------------------------------------------------//

void SyncConnector::ignoreSslErrors(QNetworkReply *reply)
{
  QList<QSslError> errorsThatCanBeIgnored;
  
  errorsThatCanBeIgnored<<QSslError(QSslError::HostNameMismatch);
  errorsThatCanBeIgnored<<QSslError(QSslError::SelfSignedCertificate);
  reply->ignoreSslErrors();
}


//------------------------------------------------------------------------------------//

void SyncConnector::onSslError(QNetworkReply* reply)
{
  reply->ignoreSslErrors();
}


//------------------------------------------------------------------------------------//

bool SyncConnector::checkIfFileExists(QString path)
{
  QFileInfo checkFile(path);
  // check if file exists and if yes: Is it really a file and not a directory?
  if (checkFile.exists() && checkFile.isFile())
  {
    return true;
  }
  else
  {
    return false;
  }
}

  
//------------------------------------------------------------------------------------//

int SyncConnector::getCurrentVersion(std::string reply)
{
  std::string separator(".");
  std::size_t pos1 = reply.find(separator);
  std::size_t pos2 = reply.find(separator, pos1+1);
  std::string result = reply.substr (pos1+1, pos2-pos1-1);
  int version = 0;
  try
  {
    version = std::stoi(result);
  }
  catch (std::exception &e)
  {
    std::cerr << "Error getting current version: " << e.what() << std::endl;
  }
  return version;
}


//------------------------------------------------------------------------------------//

void SyncConnector::killProcesses()
{
  if (mpSyncProcess != nullptr
      && mpSyncProcess->state() == QProcess::Running)
  {
    mpSyncProcess->kill();
  }
  if (mpSyncthingNotifierProcess != nullptr
      && mpSyncthingNotifierProcess->state() == QProcess::Running)
  {
    mpSyncthingNotifierProcess->kill();
  }
}


//------------------------------------------------------------------------------------//

SyncConnector::~SyncConnector()
{
  killProcesses();
}


//------------------------------------------------------------------------------------//

void QWebViewClose::closeEvent(QCloseEvent *event)
{
#pragma unused(event)
  mfk::sysutils::SystemUtility().showDockIcon(false);
  close();
}
  
} // connector
} //mfk
