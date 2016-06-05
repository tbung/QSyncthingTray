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

#ifndef utilities_h
#define utilities_h
#pragma once

#include <string>
#include <iomanip>
#include <iostream>
#include <QFile>
#include <QXmlStreamReader>
#include "platforms.hpp"

static const int kBytesToKilobytes = 1024;
// network noise floor in bytes to filter animations
static const double kNetworkNoiseFloor = 0.5;
// maximum characters for shortened filenames in UI
static const int kMaxDisplayFileNameChars = 20;

namespace qst
{
namespace utilities
{
 
template <typename T>
std::string to_string_with_precision(const T a_value, const int n = 6)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(n) << a_value;
  return out.str();
}


inline std::vector<std::string> splitFilePathByDelimiter(std::string filePath)
{
  std::vector<std::string> substrings;
  std::istringstream fileNameStream;
  fileNameStream.str(filePath);
  std::string s;
  char delimiter = sysutils::SystemUtility().getPlatformDelimiter();
  while (std::getline(fileNameStream, s, delimiter))
  {
    substrings.push_back(s);
  }
  return substrings;
}

inline QString getCleanFileName(QString fileName)
{
  std::string fileNameStd = fileName.toStdString();
  std::vector<std::string> substrings = splitFilePathByDelimiter(fileNameStd);
  
  std::string file = substrings.size() > 0 ? substrings.back() : "";
  std::string shortFile;
  if (file.length() > kMaxDisplayFileNameChars)
  {
    try
    {
      shortFile = file.substr(0, kMaxDisplayFileNameChars) + "...";
    }
    catch (std::exception &e)
    {
      std::cerr << "Failed to shorten String for UI: " << e.what() << std::endl;
      shortFile = file;
    }
  }
  else
  {
    shortFile = file;
  }
  return QString(shortFile.c_str());
}
  
inline QString getFullCleanFileName(QString fileName)
{
  std::vector<std::string> substrings = splitFilePathByDelimiter(fileName.toStdString());
  return QString(substrings.back().c_str());
}

inline std::string getPathToFileName(std::string fileName)
{
  std::vector<std::string> substrings = splitFilePathByDelimiter(fileName);
  if (substrings.size() > 1)
  {
    std::string path;
    // delete filename
    substrings.pop_back();
    char delimiter = sysutils::SystemUtility().getPlatformDelimiter();
    for (auto pathPart : substrings)
    {
      path.append(delimiter + pathPart);
    }
    return path;
  }
  else
  {
    return "";
  }
}

inline QString readAPIKey()
{
  QXmlStreamReader xmlReader;
  QFile file(qst::sysutils::SystemUtility::getDefaultSyncthingConfig());
  if (!file.open(QFile::ReadOnly | QFile::Text))
  {
    // For now return nothing as the user can input the
    // apikey on the UI should the config file be somewhere else.
    return "";
  }
  xmlReader.setDevice(&file);
  xmlReader.readNext();
  while(!xmlReader.atEnd())
  {
    if (xmlReader.name() == "apikey")
    {
      return xmlReader.readElementText();
    }
    xmlReader.readNext();
  }
  return "";
}
}
}

#endif /* utilities_h */