/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "dbtool.h"
#include "argparse.h"
#include "scoped_ptrs.h"

#include <dirent.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>

#include <cert.h>
#include <certdb.h>
#include <nss.h>
#include <prio.h>

static std::string PrintFlags(unsigned int flags) {
  std::stringstream ss;
  if ((flags & CERTDB_VALID_CA) && !(flags & CERTDB_TRUSTED_CA) &&
      !(flags & CERTDB_TRUSTED_CLIENT_CA)) {
    ss << "c";
  }
  if ((flags & CERTDB_TERMINAL_RECORD) && !(flags & CERTDB_TRUSTED)) {
    ss << "p";
  }
  if (flags & CERTDB_TRUSTED_CA) {
    ss << "C";
  }
  if (flags & CERTDB_TRUSTED_CLIENT_CA) {
    ss << "T";
  }
  if (flags & CERTDB_TRUSTED) {
    ss << "P";
  }
  if (flags & CERTDB_USER) {
    ss << "u";
  }
  if (flags & CERTDB_SEND_WARN) {
    ss << "w";
  }
  if (flags & CERTDB_INVISIBLE_CA) {
    ss << "I";
  }
  if (flags & CERTDB_GOVT_APPROVED_CA) {
    ss << "G";
  }
  return ss.str();
}

void DBTool::Usage() {
  std::cerr << "Usage: nss db [--path <directory>] [--create] --list-certs"
            << std::endl;
}

bool DBTool::Run(const std::vector<std::string> &arguments) {
  ArgParser parser(arguments);

  std::string initDir(".");
  if (parser.Has("--path")) {
    initDir = parser.Get("--path");
    if (PR_Access(initDir.c_str(), PR_ACCESS_READ_OK) != PR_SUCCESS) {
      std::cerr << "Directory '" << initDir
                << "' does not exist or you don't have permissions!"
                << std::endl;
      return false;
    }
  }

  if (!parser.Has("--list-certs") && !parser.Has("--create")) {
    return false;
  }
  std::cout << "Using database directory: " << initDir << std::endl
            << std::endl;

  bool dbFilesExist = PathHasDBFiles(initDir);
  if (parser.Has("--create") && dbFilesExist) {
    std::cerr << "Trying to create database files in a directory where they "
                 "already exists. Delete the db files before creating new ones."
              << std::endl;
    return false;
  }
  if (!parser.Has("--create") && !dbFilesExist) {
    std::cerr << "No db files found." << std::endl;
    std::cerr << "Create them using 'nss db --create [--path /foo/bar]' before "
                 "continuing."
              << std::endl;
    return false;
  }

  // init NSS
  const char *certPrefix = "";  // certutil -P option  --- can leave this empty
  SECStatus rv =
      NSS_Initialize(initDir.c_str(), certPrefix, certPrefix, "secmod.db", 0);
  if (rv != SECSuccess) {
    std::cerr << "NSS init failed!" << std::endl;
    return false;
  }

  if (parser.Has("--list-certs")) {
    ListCertificates();
  }

  if (parser.Has("--create")) {
    std::cout << "DB files created successfully." << std::endl;
  }

  // shutdown nss
  if (NSS_Shutdown() != SECSuccess) {
    std::cerr << "NSS Shutdown failed!" << std::endl;
    return false;
  }

  return true;
}

bool DBTool::PathHasDBFiles(std::string path) {
  std::regex certDBPattern("cert.*\\.db");
  std::regex keyDBPattern("key.*\\.db");

  DIR *dir;
  if (!(dir = opendir(path.c_str()))) {
    std::cerr << "Directory " << path << " could not be accessed!" << std::endl;
    return false;
  }

  struct dirent *ent;
  bool dbFileExists = false;
  while ((ent = readdir(dir))) {
    if (std::regex_match(ent->d_name, certDBPattern) ||
        std::regex_match(ent->d_name, keyDBPattern) ||
        "secmod.db" == std::string(ent->d_name)) {
      dbFileExists = true;
      break;
    }
  }

  closedir(dir);
  return dbFileExists;
}

void DBTool::ListCertificates() {
  ScopedCERTCertList list(PK11_ListCerts(PK11CertListAll, nullptr));
  CERTCertListNode *node;

  std::cout << std::setw(60) << std::left << "Certificate Nickname"
            << " "
            << "Trust Attributes" << std::endl;
  std::cout << std::setw(60) << std::left << ""
            << " "
            << "SSL,S/MIME,JAR/XPI" << std::endl
            << std::endl;

  for (node = CERT_LIST_HEAD(list); !CERT_LIST_END(node, list);
       node = CERT_LIST_NEXT(node)) {
    CERTCertificate *cert = node->cert;

    std::string name("(unknown)");
    char *appData = static_cast<char *>(node->appData);
    if (appData && strlen(appData) > 0) {
      name = appData;
    } else if (cert->nickname && strlen(cert->nickname) > 0) {
      name = cert->nickname;
    } else if (cert->emailAddr && strlen(cert->emailAddr) > 0) {
      name = cert->emailAddr;
    }

    CERTCertTrust trust;
    std::string trusts;
    if (CERT_GetCertTrust(cert, &trust) == SECSuccess) {
      std::stringstream ss;
      ss << PrintFlags(trust.sslFlags);
      ss << ",";
      ss << PrintFlags(trust.emailFlags);
      ss << ",";
      ss << PrintFlags(trust.objectSigningFlags);
      trusts = ss.str();
    } else {
      trusts = ",,";
    }
    std::cout << std::setw(60) << std::left << name << " " << trusts
              << std::endl;
  }
}
