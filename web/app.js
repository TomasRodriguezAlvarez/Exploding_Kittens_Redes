let playerId = null;
let playerName = "";
let pendingAction = null;
let lastState = null;
let lastShownMessage = "";
let lastActionType = "";

async function joinGame() {
    const name = document.getElementById("name").value.trim();
    const password = document.getElementById("password").value.trim();

    if (!name || !password) {
        document.getElementById("status").textContent =
            "Debes ingresar nombre y clave.";
        return;
    }

    const response = await fetch(
        `/api/join?name=${encodeURIComponent(name)}&password=${encodeURIComponent(password)}`
    );

    const data = await response.json();

    if (!data.ok) {
        document.getElementById("status").textContent = data.error;
        return;
    }

    playerId = data.player_id;
    playerName = data.name;
    localStorage.setItem("playerId", playerId);
    localStorage.setItem("playerName", playerName);

    document.getElementById("login").style.display = "none";
    document.getElementById("game").style.display = "block";
    document.getElementById("playerTitle").textContent =
        `Jugador ${playerId}: ${playerName}`;

    loadState();
    setInterval(loadState, 1500);
}

async function loadState() {
    if (playerId === null) return;

    const response = await fetch(`/api/state?player_id=${playerId}`);
    const data = await response.json();

    if (!data.ok) return;
    lastState = data;
    const hasEliminated = data.players.some(p => !p.alive);
    const finished = !data.game_started && hasEliminated;

    if (finished) {
        const winner = data.players.find(p => p.alive);

        document.getElementById("finishedArea").innerHTML = `
            <h3>🏆 Partida finalizada</h3>
            <p class="winner-text">
                Ganador: ${winner ? winner.name : "Sin ganador"}
            </p>

            <div class="final-players">
                ${data.players.map(p => `
                    <div class="final-player ${p.alive ? "winner" : "loser"}">
                        ${p.name} - ${p.alive ? "Ganador" : "Eliminado"}
                    </div>
                `).join("")}
            </div>

            <button onclick="logoutLocal()">Nueva partida</button>
        `;
    }

    document.getElementById("waitingRoom").style.display =
        (!data.game_started && !finished) ? "block" : "none";

    document.getElementById("gameArea").style.display =
        data.game_started ? "block" : "none";

    document.getElementById("finishedArea").style.display =
        finished ? "block" : "none";

    const drawButton = document.getElementById("drawButton");
    if (drawButton) {
        drawButton.disabled = !data.game_started || !data.is_my_turn;
    }
    

    //document.getElementById("deckCount").textContent =
        //   `${data.deck_count} cartas`;

    const messageChanged = data.last_message && data.last_message !== lastShownMessage;
    if (messageChanged) {
        lastShownMessage = data.last_message;
    }

    document.getElementById("gameInfo").innerHTML = `
        <p class="last-action ${messageChanged ? "pop" : ""}">
            ${data.last_message || ""}
        </p>
        <p><strong>Turno:</strong> ${data.players.find(p => p.id === data.current_turn)?.name || "N/A"}</p>
        <p><strong>Turnos pendientes:</strong> ${data.pending_turns}</p>
        <p><strong>Lamport:</strong> ${data.lamport}</p>
        <p><strong>Cartas en mazo:</strong> ${data.deck_count}</p>
        <p class="${data.is_my_turn ? "my-turn" : "waiting-turn"}">
            ${data.is_my_turn ? "🟢 Es tu turno" : "⏳ Esperando turno"}
        </p>
    `;

    document.getElementById("players").innerHTML =
        data.players.map(p =>
            `<div class="player-token 
                ${p.id === data.current_turn ? "active-player" : ""} 
                ${!p.alive ? "eliminated" : ""}">
                <div class="avatar">🐱</div>
                <strong>${p.name}</strong>
                <span>${p.alive ? "Vivo" : "Eliminado"}</span>
            </div>`
        ).join("");

    const waitingPlayers = document.getElementById("waitingPlayers");
    if (waitingPlayers) {
        waitingPlayers.innerHTML = data.players.map(p =>
            `<p>🐱 ${p.name}</p>`
        ).join("");
    }

    document.getElementById("hand").innerHTML =
        data.hand.map(card => {
            console.log(card.name);

            const imageMap = {
                "Ataque": "attack.png",
                "Desactivar": "defuse.png",
                "Saltar": "skip.png",
                "Favor": "favor.png",
                "Nope": "nope.png",
                "Ver el Futuro": "see_future.png",
                "Tacocat": "tacocat.png",
                "Gato Barbudo": "beard_cat.png",
                "Gato Papa Peluda": "potato_cat.png",
                "Gato Arcoiris": "rainbow_cat.png",
                "Gato Sandía": "watermelon_cat.png",
                "Gato Explosivo": "exploding.png"
            };

            let image = "back.png";

            for (const key in imageMap) {
                if (card.name.includes(key)) {
                    image = imageMap[key];
                }
            }

            return `
                <div 
                    class="card-wrapper ${selectedPair.includes(card.id) ? "selected" : ""}"
                    oncontextmenu="event.preventDefault(); selectPairCard(${card.id});"
                >
                    <img
                        src="/cards/${image}"
                        class="game-card"
                        onclick="playCard(${card.id})"
                    >
                </div>
            `;
        }).join("");
}

async function startGame() {
    const response = await fetch(`/api/start?player_id=${playerId}`);
    const data = await response.json();

    if (data.ok) soundStart();

    document.getElementById("actionStatus").textContent =
        data.ok ? data.message : data.error;

    loadState();
}

async function drawCard() {
    soundDraw();
    const response = await fetch(`/api/draw?player_id=${playerId}`);
    const data = await response.json();
    if ((data.message || "").includes("Gato Explosivo")) {
        triggerEffect("explosion-effect", 750);
        soundExplosion();
    }

    document.getElementById("actionStatus").textContent =
        data.ok ? data.message : data.error;

    loadState();
}

window.onload = function () {
    const savedId = localStorage.getItem("playerId");
    const savedName = localStorage.getItem("playerName");

    if (savedId !== null && savedName !== null) {
        playerId = Number(savedId);
        playerName = savedName;

        document.getElementById("login").style.display = "none";
        document.getElementById("game").style.display = "block";
        document.getElementById("playerTitle").textContent =
            `Jugador ${playerId}: ${playerName}`;

        loadState();
        setInterval(loadState, 1500);
    }
};

function logoutLocal() {
    localStorage.removeItem("playerId");
    localStorage.removeItem("playerName");

    playerId = null;
    playerName = "";

    location.reload();
}

async function playCard(cardId) {
    soundPlayCard();
    const card = lastState.hand.find(c => c.id === cardId);

    if (card && card.name.includes("Favor")) {
        pendingAction = { type: "play", cardId };
        openTargetModal();
        return;
    }

    await sendPlayCard(cardId, -1);
}

async function sendPlayCard(cardId, targetId) {
    const response = await fetch(
        `/api/play?player_id=${playerId}&card_id=${cardId}&target_id=${targetId}`
    );

    const data = await response.json();

    if (data.message && data.message.includes("Nope")) {
        document.body.classList.add("nope-effect");
        soundNope();

        setTimeout(() => {
            document.body.classList.remove("nope-effect");
        }, 500);
    }

    document.getElementById("actionStatus").textContent =
        data.ok ? data.message : data.error;

    loadState();
}

let selectedPair = [];

function selectPairCard(cardId) {
    if (selectedPair.includes(cardId)) {
        selectedPair = selectedPair.filter(id => id !== cardId);
    } else {
        if (selectedPair.length >= 2) selectedPair.shift();
        selectedPair.push(cardId);
    }

    loadState();
}

async function playPair() {
    soundPlayCard();
    if (selectedPair.length !== 2) {
        document.getElementById("actionStatus").textContent =
            "Debes seleccionar 2 cartas.";
        return;
    }

    pendingAction = {
        type: "playpair",
        card1Id: selectedPair[0],
        card2Id: selectedPair[1]
    };

    openTargetModal();
}

function openTargetModal() {
    const container = document.getElementById("targetOptions");

    const targets = lastState.players.filter(p =>
        p.id !== playerId && p.alive
    );

    container.innerHTML = targets.map(p =>
        `<button onclick="chooseTarget(${p.id})">
            ${p.name}
        </button>`
    ).join("");

    document.getElementById("targetModal").style.display = "flex";
}

function closeTargetModal() {
    pendingAction = null;
    document.getElementById("targetModal").style.display = "none";
}

async function chooseTarget(targetId) {
    document.getElementById("targetModal").style.display = "none";

    if (!pendingAction) return;

    if (pendingAction.type === "play") {
        await sendPlayCard(pendingAction.cardId, targetId);
    }

    if (pendingAction.type === "playpair") {
        const response = await fetch(
            `/api/playpair?player_id=${playerId}&card1_id=${pendingAction.card1Id}&card2_id=${pendingAction.card2Id}&target_id=${targetId}`
        );

        const data = await response.json();

        document.getElementById("actionStatus").textContent =
            data.ok ? data.message : data.error;

        selectedPair = [];
        loadState();
    }

    pendingAction = null;
}

function triggerEffect(className, duration = 600) {
    document.body.classList.add(className);

    setTimeout(() => {
        document.body.classList.remove(className);
    }, duration);
}

function playTone(freq, duration, type = "sine", volume = 0.08) {
    const AudioContext = window.AudioContext || window.webkitAudioContext;
    const ctx = new AudioContext();

    const osc = ctx.createOscillator();
    const gain = ctx.createGain();

    osc.type = type;
    osc.frequency.value = freq;
    gain.gain.value = volume;

    osc.connect(gain);
    gain.connect(ctx.destination);

    osc.start();

    setTimeout(() => {
        osc.stop();
        ctx.close();
    }, duration);
}

function soundClick() {
    playTone(440, 80, "square", 0.04);
}

function soundNope() {
    playTone(180, 160, "sawtooth", 0.08);
}

function soundExplosion() {
    playTone(80, 300, "sawtooth", 0.12);
}

function restartLocalGame() {
    localStorage.removeItem("player_id");
    localStorage.removeItem("player_name");

    location.reload();
}

function soundDraw() {
    playTone(520, 90, "triangle", 0.05);
    setTimeout(() => playTone(680, 80, "triangle", 0.04), 70);
}

function soundPlayCard() {
    playTone(620, 70, "square", 0.04);
    setTimeout(() => playTone(420, 90, "square", 0.035), 60);
}

function soundStart() {
    playTone(330, 100, "sine", 0.05);
    setTimeout(() => playTone(440, 100, "sine", 0.05), 90);
    setTimeout(() => playTone(660, 160, "sine", 0.06), 180);
}