import {spawn} from "node:child_process";
import {Readable, Writable} from "node:stream";
import assert from "node:assert/strict";
import * as acp from "@agentclientprotocol/sdk";

const agentPath = process.argv[2] ?? "./cmake-build-debug/buraq_echo_agent";

const child = spawn(agentPath, [], {
    stdio: ["pipe", "pipe", "inherit"],
});

const app = acp.client({name: "buraq-sdk-verifier"});

const stream = acp.ndJsonStream(
    Writable.toWeb(child.stdin),
    Readable.toWeb(child.stdout)
);

try {
    await app.connectWith(stream, async (cx) => {
        // 1. Handshake: initialize
        const initRes = await cx.request("initialize", {
            protocolVersion: acp.PROTOCOL_VERSION,
            clientCapabilities: {
                fs: {readTextFile: true, writeTextFile: true},
                terminal: true,
            },
            clientInfo: {name: "acp-official-sdk-verifier", version: "1.0.0"},
        });

        console.log("[OK] initialize:", JSON.stringify(initRes));
        assert.equal(initRes.protocolVersion, acp.PROTOCOL_VERSION);
        assert.equal(initRes.agentInfo?.name, "buraq-echo");

        // 2. Create session & stream prompt via SessionBuilder
        const session = await cx
            .buildSession({cwd: process.cwd(), mcpServers: []})
            .start();

        console.log("[OK] session/new:", session.sessionId);
        assert.ok(session.sessionId.length > 0);

        // 3. Prompt & consume streamed ActiveSessionMessage events
        const updates = [];
        const promptPromise = session.prompt("Hello from official @agentclientprotocol/sdk!");

        while (true) {
            const msg = await Promise.race([
                session.nextUpdate(),
                new Promise((_, reject) =>
                    setTimeout(() => reject(new Error("Timed out waiting for nextUpdate()")), 3000)
                ),
            ]);
            if (!msg || msg.kind === "stop") {
                break;
            }
            if (msg.kind === "session_update") {
                updates.push(msg.update);
            }
        }

        const promptRes = await promptPromise;

        console.log("[OK] streamed updates:", JSON.stringify(updates));
        console.log("[OK] session/prompt:", JSON.stringify(promptRes));

        assert.equal(promptRes.stopReason, "end_turn");
        assert.equal(updates.length, 1);
        assert.equal(updates[0].sessionUpdate, "agent_message_chunk");
        assert.equal(
            updates[0].content.text,
            "Hello from official @agentclientprotocol/sdk!"
        );

        // 4. Cancellation notification: session/cancel
        await cx.notify("session/cancel", {sessionId: session.sessionId});
        console.log("[OK] session/cancel sent cleanly");
    });

    console.log("\nALL OFFICIAL @agentclientprotocol/sdk CHECKS PASSED!");
} finally {
    child.kill();
}