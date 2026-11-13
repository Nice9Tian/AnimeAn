import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import net from 'node:net';
import path from 'node:path';

const exePath = process.argv[2] || 'build/net/ws_echo.exe';
const TOKEN = 'test_secret_token_987654';

function buildEnvWithQt() {
    const qtBins = [
        'C:/Qt/6.9.1/mingw_64/bin',
        'C:/Qt/Tools/mingw1310_64/bin',
    ].map((p) => path.normalize(p));

    const env = { ...process.env };
    const pathKey = Object.keys(env).find((k) => k.toUpperCase() === 'PATH') || 'PATH';
    env[pathKey] = [...qtBins, env[pathKey] || ''].join(path.delimiter);
    return env;
}

function startServer(executable, token) {
    return new Promise((resolve, reject) => {
        const child = spawn(executable, ['--port', '0', '--token', token], {
            env: buildEnvWithQt(),
            stdio: ['ignore', 'pipe', 'pipe'],
        });

        let stdoutBuf = '';
        let stderrBuf = '';
        let resolved = false;

        const timer = setTimeout(() => {
            if (!resolved) {
                child.kill();
                reject(new Error(`Timed out waiting for ANIMEAN_BACKEND_PORT. stdout=${stdoutBuf} stderr=${stderrBuf}`));
            }
        }, 10000);

        child.stdout.setEncoding('utf8');
        child.stdout.on('data', (chunk) => {
            stdoutBuf += chunk;
            const match = stdoutBuf.match(/ANIMEAN_BACKEND_PORT=(\d+)/);
            if (match && !resolved) {
                resolved = true;
                clearTimeout(timer);
                resolve({ child, port: Number(match[1]) });
            }
        });

        child.stderr.setEncoding('utf8');
        child.stderr.on('data', (chunk) => {
            stderrBuf += chunk;
        });

        child.on('error', (err) => {
            if (!resolved) {
                resolved = true;
                clearTimeout(timer);
                reject(err);
            }
        });

        child.on('exit', (code) => {
            if (!resolved) {
                resolved = true;
                clearTimeout(timer);
                reject(new Error(`ws_echo exited early with code ${code}. stderr=${stderrBuf}`));
            }
        });
    });
}

function waitForOpen(ws, timeoutMs = 5000) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('Timed out waiting for WebSocket open')), timeoutMs);
        ws.addEventListener('open', () => {
            clearTimeout(timer);
            resolve();
        }, { once: true });
        ws.addEventListener('error', () => {
            clearTimeout(timer);
            reject(new Error('WebSocket error before open'));
        }, { once: true });
        ws.addEventListener('close', (ev) => {
            clearTimeout(timer);
            reject(new Error(`WebSocket closed before open (code=${ev.code})`));
        }, { once: true });
    });
}

function waitForClose(ws, timeoutMs = 5000) {
    return new Promise((resolve, reject) => {
        if (ws.readyState === WebSocket.CLOSED) {
            resolve();
            return;
        }
        const timer = setTimeout(() => reject(new Error('Timed out waiting for WebSocket close')), timeoutMs);
        ws.addEventListener('close', (ev) => {
            clearTimeout(timer);
            resolve(ev);
        }, { once: true });
    });
}

function nextMessage(ws, timeoutMs = 10000) {
    return new Promise((resolve, reject) => {
        // Both listeners go once it is settled: a socket asked many times would
        // otherwise pile up close listeners that never fire.
        const settle = (finish, value) => {
            clearTimeout(timer);
            ws.removeEventListener('message', onMessage);
            ws.removeEventListener('close', onClose);
            finish(value);
        };
        const onMessage = (ev) => settle(resolve, ev.data);
        const onClose = () => settle(reject, new Error('WebSocket closed while waiting for message'));
        const timer = setTimeout(() => settle(reject, new Error('Timed out waiting for WebSocket message')), timeoutMs);
        ws.addEventListener('message', onMessage);
        ws.addEventListener('close', onClose);
    });
}

function rawHttpHandshake({ port, pathWithQuery, origin, key = 'dGhlIHNhbXBsZSBub25jZQ==' }) {
    return new Promise((resolve, reject) => {
        const socket = net.createConnection({ host: '127.0.0.1', port });
        let buf = Buffer.alloc(0);
        const timer = setTimeout(() => {
            socket.destroy();
            reject(new Error('Timed out in rawHttpHandshake'));
        }, 5000);

        socket.on('connect', () => {
            const lines = [
                `GET ${pathWithQuery} HTTP/1.1`,
                `Host: 127.0.0.1:${port}`,
                'Upgrade: websocket',
                'Connection: Upgrade',
                `Sec-WebSocket-Key: ${key}`,
                'Sec-WebSocket-Version: 13',
            ];
            if (origin !== undefined) {
                lines.push(`Origin: ${origin}`);
            }
            lines.push('', '');
            socket.write(lines.join('\r\n'));
        });

        socket.on('data', (chunk) => {
            buf = Buffer.concat([buf, chunk]);
            const idx = buf.indexOf('\r\n\r\n');
            if (idx !== -1) {
                clearTimeout(timer);
                const headerStr = buf.subarray(0, idx).toString('utf8');
                const rest = buf.subarray(idx + 4);
                const statusLine = headerStr.split('\r\n')[0];
                resolve({ socket, statusLine, headerStr, rest });
            }
        });

        socket.on('error', (err) => {
            clearTimeout(timer);
            reject(err);
        });
    });
}

function buildMaskedFrame(opcode, payloadBuf, fin = true, mask = [0x37, 0xfa, 0x21, 0x3d]) {
    const len = payloadBuf.length;
    let header;
    if (len <= 125) {
        header = Buffer.alloc(2 + 4);
        header[0] = (fin ? 0x80 : 0x00) | (opcode & 0x0f);
        header[1] = 0x80 | len;
        Buffer.from(mask).copy(header, 2);
    } else if (len <= 65535) {
        header = Buffer.alloc(4 + 4);
        header[0] = (fin ? 0x80 : 0x00) | (opcode & 0x0f);
        header[1] = 0x80 | 126;
        header.writeUInt16BE(len, 2);
        Buffer.from(mask).copy(header, 4);
    } else {
        throw new Error('Unsupported length in test helper');
    }
    const maskedPayload = Buffer.alloc(len);
    for (let i = 0; i < len; i++) {
        maskedPayload[i] = payloadBuf[i] ^ mask[i & 3];
    }
    return Buffer.concat([header, maskedPayload]);
}

async function runTests() {
    assert.equal(typeof WebSocket, 'function', 'Global WebSocket must be available (Node 24)');
    const { child, port } = await startServer(exePath, TOKEN);
    console.log(`[PASS] Server listening on 127.0.0.1:${port}`);

    try {
        // 1. 错 token 被拒
        {
            const { socket, statusLine } = await rawHttpHandshake({
                port,
                pathWithQuery: '/?token=wrong_token',
            });
            assert.match(statusLine, /^HTTP\/1\.1 401 /);
            socket.destroy();

            const badWs = new WebSocket(`ws://127.0.0.1:${port}/?token=wrong_token`);
            let opened = false;
            badWs.addEventListener('open', () => {
                opened = true;
            });
            await waitForClose(badWs);
            assert.equal(opened, false, 'WebSocket with wrong token must not open');
            console.log('[PASS] 1. Wrong token rejected (HTTP 401 & WebSocket rejected)');
        }

        // 2. 对 token 能连 & 3. 文本回显
        {
            const ws = new WebSocket(`ws://127.0.0.1:${port}/?token=${TOKEN}`);
            await waitForOpen(ws);
            console.log('[PASS] 2. Valid token connected');

            const textSamples = [
                '{"t":"call","id":1,"method":"layer.add","params":{"view":"main","name":"图层一"}}',
                'A'.repeat(70000), // 16-bit extended payload length (>65535 bytes) still fits in 64-bit, wait: 70000 is 64-bit length, 30000 is 16-bit length
                'B'.repeat(30000), // 16-bit length (126..65535)
                '',                // empty text frame
            ];
            for (const sample of textSamples) {
                const replyPromise = nextMessage(ws);
                ws.send(sample);
                const reply = await replyPromise;
                assert.equal(typeof reply, 'string');
                assert.equal(reply.length, sample.length);
                assert.equal(reply, sample);
            }
            console.log('[PASS] 3. Text echo verified (UTF-8 JSON, empty, 16-bit & 64-bit lengths)');

            // 4. 8 MB 二进制回显逐字节相等
            ws.binaryType = 'arraybuffer';
            const size8MB = 8 * 1024 * 1024;
            const binSend = new Uint8Array(size8MB);
            for (let i = 0; i < size8MB; i++) {
                binSend[i] = (i * 131 + (i >>> 8) * 17 + (i >>> 16) * 31) & 0xff;
            }
            const binReplyPromise = nextMessage(ws, 20000);
            ws.send(binSend);
            const binReplyBuf = await binReplyPromise;
            assert.ok(binReplyBuf instanceof ArrayBuffer, 'Expected ArrayBuffer binary reply');
            const binRecv = new Uint8Array(binReplyBuf);
            assert.equal(binRecv.byteLength, size8MB, '8 MB echo byteLength must match');
            assert.equal(
                Buffer.compare(
                    Buffer.from(binRecv.buffer, binRecv.byteOffset, binRecv.byteLength),
                    Buffer.from(binSend.buffer, binSend.byteOffset, binSend.byteLength),
                ),
                0,
                '8 MB binary echo must match byte-for-byte',
            );
            console.log('[PASS] 4. 8 MB binary echo verified byte-for-byte');

            // 5. 连续 1000 条小消息顺序不乱
            const totalSmall = 1000;
            const receivedSmall = [];
            const allSmallReceived = new Promise((resolve, reject) => {
                const timer = setTimeout(() => {
                    reject(new Error(`Timed out at ${receivedSmall.length}/${totalSmall} small messages`));
                }, 15000);
                const onMsg = (ev) => {
                    receivedSmall.push(ev.data);
                    if (receivedSmall.length === totalSmall) {
                        clearTimeout(timer);
                        ws.removeEventListener('message', onMsg);
                        resolve();
                    }
                };
                ws.addEventListener('message', onMsg);
            });

            for (let i = 0; i < totalSmall; i++) {
                ws.send(`msg-${i}`);
            }
            await allSmallReceived;
            assert.equal(receivedSmall.length, totalSmall);
            for (let i = 0; i < totalSmall; i++) {
                assert.equal(receivedSmall[i], `msg-${i}`, `Order mismatch at index ${i}`);
            }
            console.log('[PASS] 5. 1000 consecutive small messages echoed in exact order');

            // 6. 第二个连接进来，第一个留着
            const ask = async (socket, text) => {
                const reply = nextMessage(socket);
                socket.send(text);
                return reply;
            };
            // Every message a socket gets from now on, in order.
            const collect = (socket) => {
                const got = [];
                socket.addEventListener('message', (ev) => got.push(ev.data));
                return got;
            };
            const settle = () => new Promise((resolve) => setTimeout(resolve, 200));

            const ws2 = new WebSocket(`ws://127.0.0.1:${port}/?token=${TOKEN}`);
            ws2.binaryType = 'arraybuffer';
            await waitForOpen(ws2);
            assert.equal(ws.readyState, WebSocket.OPEN, 'The first connection must stay open when a second one connects');
            assert.equal(await ask(ws, 'clients?'), 'clients:2');
            assert.equal(await ask(ws2, 'clients?'), 'clients:2');
            console.log('[PASS] 6. A second connection joins and the first one stays');

            // 6a. 定向发送只到发的那一个
            {
                const got1 = collect(ws);
                const got2 = collect(ws2);
                ws.send('only-for-ws1');
                ws2.send('only-for-ws2');
                ws2.send(new Uint8Array([1, 2, 3, 4]));
                await settle();
                assert.deepEqual(got1, ['only-for-ws1'], 'A reply goes to the client that asked, and to no other');
                assert.equal(got2.length, 2);
                assert.equal(got2[0], 'only-for-ws2');
                assert.deepEqual([...new Uint8Array(got2[1])], [1, 2, 3, 4]);
                console.log('[PASS] 6a. A message sent to one client reaches only that client');
            }

            // 6b. 广播到每一个，各自的顺序不乱
            {
                const got1 = collect(ws);
                const got2 = collect(ws2);
                const total = 200;
                for (let i = 0; i < total; i++) {
                    (i % 2 === 0 ? ws : ws2).send(`all:${i}`);
                }
                ws.send(new TextEncoder().encode('all:binary'));
                const deadline = Date.now() + 10000;
                while ((got1.length < total + 1 || got2.length < total + 1) && Date.now() < deadline) {
                    await settle();
                }
                const texts = (got) => got.filter((m) => typeof m === 'string');
                for (const [name, got] of [['ws1', got1], ['ws2', got2]]) {
                    assert.equal(got.length, total + 1, `${name} must get every broadcast message`);
                    const evens = texts(got).filter((m) => Number(m.slice(4)) % 2 === 0);
                    const odds = texts(got).filter((m) => Number(m.slice(4)) % 2 === 1);
                    assert.deepEqual(evens, Array.from({ length: total / 2 }, (_, i) => `all:${i * 2}`), `${name}: ws1's messages in order`);
                    assert.deepEqual(odds, Array.from({ length: total / 2 }, (_, i) => `all:${i * 2 + 1}`), `${name}: ws2's messages in order`);
                    const binary = got.find((m) => typeof m !== 'string');
                    assert.equal(new TextDecoder().decode(binary), 'all:binary', `${name} must get the broadcast binary frame`);
                }
                console.log('[PASS] 6b. A broadcast reaches every client, each sender\'s messages in order');
            }

            // 6c. 一个连接出错被断开，其余的不受影响
            {
                const { socket: rogue, statusLine } = await rawHttpHandshake({ port, pathWithQuery: `/?token=${TOKEN}` });
                assert.match(statusLine, /^HTTP\/1\.1 101 /);
                assert.equal(await ask(ws, 'clients?'), 'clients:3');
                const rogueClosed = new Promise((resolve) => rogue.on('close', resolve));
                rogue.write(Buffer.from([0x81, 0x05, 0x68, 0x65, 0x6c, 0x6c, 0x6f])); // unmasked: a protocol error
                await rogueClosed;
                assert.equal(await ask(ws, 'clients?'), 'clients:2', 'Only the client that broke the protocol is dropped');
                assert.equal(await ask(ws2, 'still-here'), 'still-here');
                console.log('[PASS] 6c. A client dropped for a protocol error leaves the others connected');
            }

            // 6d. 一个连接自己关掉，其余的照常
            {
                const ws2Closed = waitForClose(ws2);
                ws2.close();
                await ws2Closed;
                await settle();
                assert.equal(ws.readyState, WebSocket.OPEN);
                assert.equal(await ask(ws, 'clients?'), 'clients:1');
                const got1 = collect(ws);
                ws.send('all:alone');
                await settle();
                assert.deepEqual(got1, ['all:alone'], 'A broadcast with one client left still arrives, once');
                console.log('[PASS] 6d. A client that leaves is forgotten and the rest carry on');
            }

            // 6e. 很多连接同时在
            {
                const many = [];
                for (let i = 0; i < 12; i++) {
                    const extra = new WebSocket(`ws://127.0.0.1:${port}/?token=${TOKEN}`);
                    await waitForOpen(extra);
                    many.push(extra);
                }
                assert.equal(await ask(ws, 'clients?'), 'clients:13');
                const inboxes = many.map(collect);
                ws.send('all:everyone');
                await settle();
                for (const inbox of inboxes) {
                    assert.deepEqual(inbox, ['all:everyone']);
                }
                await Promise.all(many.map((extra) => {
                    const closed = waitForClose(extra);
                    extra.close();
                    return closed;
                }));
                await settle();
                assert.equal(await ask(ws, 'clients?'), 'clients:1');
                console.log('[PASS] 6e. Thirteen clients at once, then back to one');
            }

            const wsClosed = waitForClose(ws);
            ws.close();
            await wsClosed;
        }

        // 7. 非法 Origin 被拒（用 net 模块手写握手）
        {
            const badOrigins = [
                'https://evil.example.com',
                'http://127.0.0.1:5173',
                'http://localhost',
                'http://localhost:abc',
                'http://localhost:70000',
            ];
            for (const badOrigin of badOrigins) {
                const { socket, statusLine } = await rawHttpHandshake({
                    port,
                    pathWithQuery: `/?token=${TOKEN}`,
                    origin: badOrigin,
                });
                assert.match(statusLine, /^HTTP\/1\.1 403 /, `Origin ${badOrigin} must be rejected with 403`);
                await new Promise((resolve) => {
                    if (socket.destroyed) return resolve();
                    socket.on('close', resolve);
                });
            }

            const goodOrigins = [
                'tauri://localhost',
                'http://tauri.localhost',
                'https://tauri.localhost',
                'http://localhost:5173',
            ];
            for (const goodOrigin of goodOrigins) {
                const { socket, statusLine } = await rawHttpHandshake({
                    port,
                    pathWithQuery: `/?token=${TOKEN}`,
                    origin: goodOrigin,
                });
                assert.match(statusLine, /^HTTP\/1\.1 101 /, `Origin ${goodOrigin} must be allowed with 101`);
                socket.destroy();
            }
            console.log('[PASS] 7. Invalid Origins rejected (HTTP 403) and valid Origins accepted');
        }

        // 8. 未掩码帧被断开（手写） + 分片重组与 Ping/Pong 验证
        {
            // First verify fragmented frame reassembly and Ping -> Pong over raw socket
            const { socket: fragSocket, statusLine: fragStatus } = await rawHttpHandshake({
                port,
                pathWithQuery: `/?token=${TOKEN}`,
            });
            assert.match(fragStatus, /^HTTP\/1\.1 101 /);

            const fragReplies = await new Promise((resolve, reject) => {
                let buf = Buffer.alloc(0);
                const frames = [];
                const timer = setTimeout(() => reject(new Error('Timed out waiting for fragmented/pong frames')), 5000);
                fragSocket.on('data', (chunk) => {
                    buf = Buffer.concat([buf, chunk]);
                    while (buf.length >= 2) {
                        const opcode = buf[0] & 0x0f;
                        const len = buf[1] & 0x7f;
                        if (buf.length < 2 + len) break;
                        const payload = buf.subarray(2, 2 + len);
                        buf = buf.subarray(2 + len);
                        frames.push({ opcode, payload: payload.toString('utf8') });
                        if (frames.length === 2) {
                            clearTimeout(timer);
                            resolve(frames);
                        }
                    }
                });

                // Send Fragment 1 (opcode=1 Text, FIN=0): "Hello, "
                fragSocket.write(buildMaskedFrame(0x1, Buffer.from('Hello, '), false));
                // Interleave a Ping frame (opcode=9 Ping, FIN=1): "ping-payload"
                fragSocket.write(buildMaskedFrame(0x9, Buffer.from('ping-payload'), true));
                // Send Fragment 2 (opcode=0 Continuation, FIN=1): "Fragmented World!"
                fragSocket.write(buildMaskedFrame(0x0, Buffer.from('Fragmented World!'), true));
            });

            assert.deepEqual(fragReplies[0], { opcode: 0x0a, payload: 'ping-payload' });
            assert.deepEqual(fragReplies[1], { opcode: 0x01, payload: 'Hello, Fragmented World!' });
            fragSocket.destroy();

            // Now test unmasked frame disconnection
            const { socket, statusLine } = await rawHttpHandshake({
                port,
                pathWithQuery: `/?token=${TOKEN}`,
            });
            assert.match(statusLine, /^HTTP\/1\.1 101 /);

            const closeResult = await new Promise((resolve, reject) => {
                let receivedBuf = Buffer.alloc(0);
                const timer = setTimeout(() => {
                    socket.destroy();
                    reject(new Error('Timed out waiting for server to disconnect on unmasked frame'));
                }, 5000);

                socket.on('data', (chunk) => {
                    receivedBuf = Buffer.concat([receivedBuf, chunk]);
                });
                socket.on('close', () => {
                    clearTimeout(timer);
                    resolve(receivedBuf);
                });

                // Send unmasked text frame: FIN=1, opcode=0x1, MASK=0, len=5, payload="hello"
                const unmaskedFrame = Buffer.from([0x81, 0x05, 0x68, 0x65, 0x6c, 0x6c, 0x6f]);
                socket.write(unmaskedFrame);
            });

            // If server sends a frame before closing, it must be a Close frame (0x88) with code 1002 (0x03ea), NOT a text echo (0x81)
            if (closeResult.length > 0) {
                assert.equal(closeResult[0], 0x88, 'Server response to unmasked frame must be Close frame (0x88), not echo');
                if (closeResult.length >= 4) {
                    assert.equal(closeResult.readUInt16BE(2), 1002, 'Close status code must be 1002 Protocol Error');
                }
            }
            console.log('[PASS] 8. Unmasked client frame disconnected with protocol error (plus fragmentation & ping/pong verified)');
        }

        console.log('ALL WS_SERVER TESTS PASSED');
    } finally {
        child.kill();
    }
}

runTests().catch((err) => {
    console.error('[FAIL]', err);
    process.exitCode = 1;
});
