/*
 * Copyright (c) 2018-present, Frederick Emmott.
 * All rights reserved.
 *
 * This source code is licensed under the MIT license found in the LICENSE file
 * in the root directory of this source tree.
 */

#include "ClientHandler.h"

#include <sodium.h>

#include <cassert>
#include <memory>

#include "MessageInterface.h"
#include "StreamingSoftware.h"

using json = nlohmann::json;

#define clean_and_return() \
  delete this; \
  return;

#define clean_and_return_unless(x) \
  if (!(x)) { \
    clean_and_return(); \
  }

ClientHandler::ClientHandler(
  StreamingSoftware* software,
  MessageInterface* connection)
  : mSoftware(software),
    mConnection(connection),
    mState(ClientState::UNINITIALIZED) {
  mSoftware->outputStateChanged.connect(
    this, &ClientHandler::outputStateChanged);
  mConnection->messageReceived.connect(this, &ClientHandler::messageReceived);
  mConnection->disconnected.connect([this]() {
    mConnection = nullptr;
    delete this;
  });
}

ClientHandler::~ClientHandler() {
  delete mConnection;
  cleanCrypto();
}

void ClientHandler::messageReceived(const std::string& message) {
  switch (mState) {
    case ClientState::UNINITIALIZED:
      handshakeClientHelloMessageReceived(message);
      return;
    case ClientState::WAITING_FOR_CLIENT_READY:
      handshakeClientReadyMessageReceived(message);
      return;
    case ClientState::AUTHENTICATED:
      encryptedRpcMessageReceived(message);
      return;
  }
}

void ClientHandler::encryptedRpcMessageReceived(const std::string& c) {
  // No variable length arrays on MSVC :'(
  const size_t psize = c.size() - crypto_secretstream_xchacha20poly1305_ABYTES;
  auto p = std::unique_ptr<unsigned char>(new unsigned char[psize]);
  unsigned long long plen;
  unsigned char tag;
  const auto result = crypto_secretstream_xchacha20poly1305_pull(
    &this->mCryptoPullState, p.get(), &plen, &tag,
    reinterpret_cast<const unsigned char*>(c.data()), c.size(), nullptr, 0);
  clean_and_return_unless(result == 0);
  assert(plen <= psize);
  plaintextRpcMessageReceived(
    std::string(reinterpret_cast<const char*>(p.get()), plen));
}

void ClientHandler::plaintextRpcMessageReceived(const std::string& message) {
  auto jsonrpc = json::parse(message);
  if (jsonrpc["jsonrpc"] != "2.0") {
    clean_and_return();
  }

  std::string method = jsonrpc["method"];

  if (method == "outputs/get") {
    const auto outputs = mSoftware->getOutputs();
    json outputsJson;
    for (const auto& output : outputs) {
      outputsJson[output.id] = output.toJson();
    }

    encryptThenSendMessage(
      (json{{"jsonrpc", "2.0"}, {"id", jsonrpc["id"]}, {"result", outputsJson}})
        .dump());
    return;
  }

  if (method == "outputs/start") {
    mSoftware->startOutput(jsonrpc["params"]["id"]);
    encryptThenSendMessage(
      {{"jsonrpc", "2.0"}, {"id", jsonrpc["id"]}, {"result", json::object()}});
    return;
  }

  if (method == "outputs/stop") {
    mSoftware->stopOutput(jsonrpc["params"]["id"]);
    encryptThenSendMessage(
      {{"jsonrpc", "2.0"}, {"id", jsonrpc["id"]}, {"result", json::object()}});
    return;
  }

  if (method == "outputs/setDelay") {
    const bool success = mSoftware->setOutputDelay(
      jsonrpc["params"]["id"], jsonrpc["params"]["seconds"]);
    json response{
      {"jsonrpc", "2.0"},
      {"id", jsonrpc["id"]},
    };
    if (success) {
      response["result"] = json::object();
    } else {
      response["error"] = json{
        {"code", 0}, {"message", "The software failed to set the delay"}};
    }
    encryptThenSendMessage(response);
    return;
  }
}

namespace {
#pragma pack(push, 1)
struct ClientHelloBox {
  uint8_t serverToClientKey[crypto_secretstream_xchacha20poly1305_KEYBYTES];
};
struct ClientHelloMessage {
  uint8_t pwhashSalt[crypto_pwhash_SALTBYTES];
  uint8_t secretBoxNonce[crypto_secretbox_NONCEBYTES];
  uint8_t secretBox[sizeof(ClientHelloBox) + crypto_secretbox_MACBYTES];
};
struct ServerHelloBox {
  uint8_t clientToServerKey[crypto_secretstream_xchacha20poly1305_KEYBYTES];
  uint8_t authenticationKey[crypto_auth_KEYBYTES];
};
struct ServerHelloMessage {
  uint8_t secretBoxNonce[crypto_secretbox_NONCEBYTES];
  uint8_t secretBox[sizeof(ServerHelloBox) + crypto_secretbox_MACBYTES];
  uint8_t
    serverToClientHeader[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
};
#pragma pack(pop)
}// namespace

void ClientHandler::handshakeClientHelloMessageReceived(
  const std::string& blob) {
  clean_and_return_unless(blob.size() == sizeof(ClientHelloMessage));
  const ClientHelloMessage* request
    = reinterpret_cast<const ClientHelloMessage*>(blob.data());
  ClientHelloBox requestBox;
  ServerHelloBox responseBox;
  ServerHelloMessage response;

  // Open the box!
  const auto password = mSoftware->getConfiguration().password;
  uint8_t psk[crypto_secretbox_KEYBYTES];
  {
    const auto result = crypto_pwhash(
      psk, crypto_secretbox_KEYBYTES, password.data(), password.size(),
      request->pwhashSalt, crypto_pwhash_OPSLIMIT_INTERACTIVE,
      crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_DEFAULT);
    clean_and_return_unless(result == 0);
  }
  {
    const auto result = crypto_secretbox_open_easy(
      reinterpret_cast<uint8_t*>(&requestBox), request->secretBox,
      sizeof(request->secretBox), request->secretBoxNonce, psk);
    clean_and_return_unless(result == 0);
  }

  // Process and respond
  {
    const auto result = crypto_secretstream_xchacha20poly1305_init_push(
      &this->mCryptoPushState, response.serverToClientHeader,
      requestBox.serverToClientKey);
    clean_and_return_unless(result == 0);
  }

  crypto_secretstream_xchacha20poly1305_keygen(responseBox.clientToServerKey);
  crypto_auth_keygen(responseBox.authenticationKey);
  static_assert(
    sizeof(responseBox.clientToServerKey) == sizeof(this->mPullKey),
    "differenting pull key sizes");
  memcpy(
    this->mPullKey, responseBox.clientToServerKey,
    sizeof(responseBox.clientToServerKey));
  static_assert(
    sizeof(responseBox.authenticationKey) == sizeof(this->mAuthenticationKey),
    "differing authentication key sizes");
  memcpy(
    this->mAuthenticationKey, responseBox.authenticationKey,
    sizeof(responseBox.authenticationKey));

  randombytes_buf(response.secretBoxNonce, sizeof(response.secretBoxNonce));
  {
    const auto result = crypto_secretbox_easy(
      response.secretBox, reinterpret_cast<const uint8_t*>(&responseBox),
      sizeof(responseBox), response.secretBoxNonce, psk);
    clean_and_return_unless(result == 0);
  }
  this->mState = ClientState::WAITING_FOR_CLIENT_READY;
  this->mConnection->sendMessage(
    std::string(reinterpret_cast<const char*>(&response), sizeof(response)));
}

void ClientHandler::outputStateChanged(
  const std::string& id,
  OutputState state) {
  encryptThenSendMessage(
    {{"jsonrpc", "2.0"},
     {"method", "outputs/stateChanged"},
     {"params", json{{"id", id}, {"state", Output::stateToString(state)}}}});
}

namespace {
#pragma pack(push, 1)
struct ClientReadyMessage {
  uint8_t
    clientToServerHeader[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
  uint8_t authenticationMac[crypto_auth_BYTES];
};
#pragma pack(pop)
}// namespace

void ClientHandler::handshakeClientReadyMessageReceived(
  const std::string& blob) {
  clean_and_return_unless(blob.size() == sizeof(ClientReadyMessage));
  const ClientReadyMessage* request
    = reinterpret_cast<const ClientReadyMessage*>(blob.data());
  {
    const int result = crypto_auth_verify(
      request->authenticationMac, request->clientToServerHeader,
      sizeof(request->clientToServerHeader), this->mAuthenticationKey);
    clean_and_return_unless(result == 0);
  }
  {
    const int result = crypto_secretstream_xchacha20poly1305_init_pull(
      &this->mCryptoPullState, request->clientToServerHeader, this->mPullKey);
    clean_and_return_unless(result == 0);
  }

  this->mState = ClientState::AUTHENTICATED;

  this->encryptThenSendMessage({{"jsonrpc", "2.0"}, {"method", "hello"}});
}

void ClientHandler::cleanCrypto() {
  sodium_memzero(&this->mCryptoPullState, sizeof(this->mCryptoPullState));
  sodium_memzero(&this->mCryptoPushState, sizeof(this->mCryptoPushState));
}

void ClientHandler::encryptThenSendMessage(const json& message) {
  encryptThenSendMessage(message.dump());
}

void ClientHandler::encryptThenSendMessage(const std::string& p) {
  if (this->mState != ClientState::AUTHENTICATED) {
    return;
  }

  auto csize = p.size() + crypto_secretstream_xchacha20poly1305_ABYTES;
  auto c = std::unique_ptr<unsigned char>(new unsigned char[csize]);
  unsigned long long clen;
  const auto result = crypto_secretstream_xchacha20poly1305_push(
    &this->mCryptoPushState, c.get(), &clen,
    reinterpret_cast<const unsigned char*>(p.data()), p.size(), nullptr, 0, 0);
  assert(clen <= csize);
  clean_and_return_unless(result == 0);
  mConnection->sendMessage(
    std::string(reinterpret_cast<const char*>(c.get()), clen));
}
