/*
 * Copyright (c) 2018-present, Frederick Emmott.
 * All rights reserved.
 *
 * This source code is licensed under the MIT license found in the LICENSE file
 * in the root directory of this source tree.
 */

import handshake from './handshake';
import RPCClient from './RPCClient';
import CryptoState from './CryptoState';
import { Output, OutputType, OutputState } from './RPCTypes';
import { ClientConfig, ClientConfigStorage } from './ClientConfig';

function handleOutputDrop(e) {
  e.preventDefault();
}

function handleOutputDrag(e) {
  const dragging = e.target;
  const container = document.querySelector('#outputContainer');
  for (let i = 0; i < container.children.length; ++i) {
    const child = container.children[i];
    if (child == dragging) {
      continue;
    }
    if (!(child instanceof HTMLElement)) {
      continue;
    }
    if (child.offsetLeft > e.clientX) {
      container.insertBefore(dragging, child);
      return;
    }
  }
  container.appendChild(dragging);
}

function handleOutputDragOver(e) {
  e.preventDefault();
}

class WebUIClient {
  private ws: WebSocket;
  private rpc: RPCClient;
  constructor(ws: WebSocket) {
    this.ws = ws;
  }

  public async start(password: string): Promise<void> {
    let handshakeState: CryptoState = null;
    try {
      handshakeState = await handshake(this.ws, password);
    } catch (e) {
      const error = document.querySelector('#error');
      error.classList.remove('uninit');
      if (typeof e == 'string') {
        error.textContent = e;
      } else if (e instanceof Error) {
        error.textContent = e.message;
      }
      document.querySelector('#connectionParameters').classList.remove('hide');
      return;
    }
    this.rpc = new RPCClient(this.ws, handshakeState);
    this.rpc.onHelloNotification(() => {
      document.querySelector('#connecting').classList.add('hide');
      this.updateOutputs();
    });
    this.rpc.onOutputStateChanged((id: string, state: OutputState) => {
      const element = document.querySelector(
        '.output[data-output-id="' + id + '"]'
      ) as HTMLElement;
      this.setOutputElementState(element, state);
    });
  }

  private async updateOutputs(): Promise<void> {
    const outputs = await this.rpc.getOutputs();
    const container = document.querySelector('#outputContainer');
    while (container.hasChildNodes()) {
      container.removeChild(container.firstChild);
    }

    for (const id in outputs) {
      this.addOutputElement(outputs[id]);
    }
    document.querySelector('#outputContainer').classList.remove('uninit');
  }

  private addOutputElement(output: Output): void {
    const recordingTemplate = document.querySelector('#recordingTemplate');
    const streamingTemplate = document.querySelector('#streamingTemplate');
    const container = document.querySelector('#outputContainer');
    const { id, name, type, state } = output;
    const template = (type === OutputType.LOCAL_RECORDING)
      ? recordingTemplate : streamingTemplate;
    const elem = template.cloneNode(/* deep = */ true) as HTMLElement;
    elem.dataset.outputId = id;
    elem.removeAttribute('id');
    this.setOutputElementState(elem, state);
    elem.querySelector('.button').addEventListener('click', e => {
      e.preventDefault();
      this.toggleState(id, elem);
    });
    elem.querySelector('h1').textContent = name;
    elem.addEventListener('drag', e => handleOutputDrag(e));
    elem.addEventListener('drop', e => handleOutputDrop(e));
    elem.querySelector('.remove').addEventListener('click', e => {
      e.preventDefault();
      elem.classList.add('removed');
    });
    container.appendChild(elem);
  }

  private toggleState(id: string, node: HTMLElement): void {
    if (node.classList.contains('stopped')) {
      this.rpc.startOutput(id);
      return;
    }
    if (node.classList.contains('active')) {
      this.rpc.stopOutput(id);
      return;
    }
  }

  private setOutputElementState(node: HTMLElement, state: OutputState): void {
    const classes = node.classList;
    classes.remove('starting', 'active', 'stopping', 'stopped', 'uninit');
    classes.add(state);

    const label = state === OutputState.ACTIVE ? 'LIVE' : (state.charAt(0).toUpperCase() + state.slice(1));
    node.querySelector('.state').textContent = label;
  }
}

async function connect(config: ClientConfig) {
  document.querySelector('#connectionParameters').classList.add('hide');
  document.querySelector('#connecting').classList.remove('hide');
  const uri = 'ws://' + config.host + ':' + config.port;
  const password = config.password;
  const ws = new WebSocket(uri);
  ws.binaryType = 'arraybuffer';

  ws.addEventListener('open', async () => {
    const client = (new WebUIClient(ws));
    await client.start(password);
    ws.addEventListener('close', function () {
      reset(ws, config);
    });
  });

  ws.addEventListener('error', function () {
    reset(ws, config);
  });
}

function reset(ws: WebSocket, config: ClientConfig) {
  ws.close();
  setTimeout(function () { connect(config); }, 500);
}

function initializeConnectionForm(): void {
  try {
    const saved = ClientConfigStorage.getSavedConfigurationNames();
    if (saved.length == 0) {
      document.querySelector('#savedConnections').classList.add('hide');
    } else {
      const list = document.querySelector('#savedConnectionsList') as HTMLElement;
      saved.forEach(name => {
        const label = document.createElement('span');
        label.textContent = name;
        const button = document.createElement('button');
        button.textContent = "Connect";
        list.appendChild(label);
        list.appendChild(button);

        button.addEventListener('click', e => {
          e.preventDefault();
          const config = ClientConfigStorage.getSavedConfiguration(name);
          document.title = name + ' [Stream Remote]';
          connect(config);
        });
      });
    }
  } catch (e) {
    // Safari forbids local storage on file:// URIs
    const toHide = document.querySelectorAll('.needsLocalStorage');
    Array.prototype.forEach.call(toHide, node => node.classList.add('hide'));
  }

  document.querySelector('#connectionParameters').classList.remove('uninit');
  document.querySelector('#connectButton').addEventListener('click', e => {
    e.preventDefault();
    const config = {
      host: (document.querySelector('#host') as HTMLInputElement).value,
      password: (document.querySelector('#password') as HTMLInputElement).value,
      port: parseInt((document.querySelector('#port') as HTMLInputElement).value),
    };
    const name = (document.querySelector('#name') as HTMLInputElement).value;
    if (name !== '') {
      document.title = name + ' [Stream Remote]';
      ClientConfigStorage.saveConfiguration(name, config);
    }
    connect(config);
  });
}

window.addEventListener('load', async function () {
  initializeConnectionForm();
  document.querySelector('#outputContainer').addEventListener('dragover', e => handleOutputDragOver(e));
});
