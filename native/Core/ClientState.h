/*
 * Copyright (c) 2018-present, Frederick Emmott.
 * All rights reserved.
 *
 * This source code is licensed under the MIT license found in the LICENSE file
 * in the root directory of this source tree.
 */

#pragma once

enum class ClientState {
  UNINITIALIZED,
  WAITING_FOR_CLIENT_READY,
  AUTHENTICATED
};
