let lastServerTime = null;
let lastUpdateTimestamp = 0;

function updateDashboard() {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 5000);

    fetch('/status', { signal: controller.signal })
        .then(response => {
            clearTimeout(timeoutId);
            if (!response.ok) throw new Error('Offline');
            return response.json();
        })
        .then(data => {
            // Оновлення статусу
            const statusBadge = document.getElementById('connection-status');
            statusBadge.innerText = 'В мережі';
            statusBadge.classList.remove('status-offline');
            statusBadge.classList.add('status-online');

            // Оновлення часу
            if (data.time) {
                lastServerTime = data.time;
                lastUpdateTimestamp = Date.now();
                document.getElementById('currentTime').innerText = data.time;
            }
            
            // Оновлення дати
            if (data.date) {
                document.getElementById('currentDate').innerText = data.date;
            }

            // Оновлення температури
            if (data.temperature !== undefined) {
                document.getElementById('tempValue').innerText = data.temperature.toFixed(1);
            }

            // Оновлення вологості
            if (data.humidity !== undefined) {
                document.getElementById('humValue').innerText = data.humidity.toFixed(1);
            }

            // Оновлення яскравості
            if (data.brightness !== undefined) {
                const slider = document.getElementById('brightnessSlider');
                const label = document.getElementById('brightnessLabel');
                if (slider && label && document.activeElement !== slider) {
                    slider.value = data.brightness;
                    label.innerText = data.brightness;
                }
            }

            // Оновлення логів
            if (data.logs && Array.isArray(data.logs)) {
                const logsDiv = document.getElementById('logs-container');
                if (data.logs.length > 0) {
                    logsDiv.innerHTML = data.logs.map(log => `<div class="log-line">${log}</div>`).join('');
                } else if (logsDiv.innerHTML === '') {
                    logsDiv.innerHTML = '<div class="log-line">Логи відсутні...</div>';
                }
            }
        })
        .catch(error => {
            clearTimeout(timeoutId);
            console.error('Помилка отримання даних:', error);
            const statusBadge = document.getElementById('connection-status');
            statusBadge.innerText = 'Офлайн';
            statusBadge.classList.remove('status-online');
            statusBadge.classList.add('status-offline');
        });
}

function tick() {
    if (!lastServerTime) return;

    const [h, m, s] = lastServerTime.split(':').map(Number);
    const elapsedMs = Date.now() - lastUpdateTimestamp;
    
    const date = new Date();
    date.setHours(h, m, s, 0);
    date.setMilliseconds(date.getMilliseconds() + elapsedMs);

    const hh = String(date.getHours()).padStart(2, '0');
    const mm = String(date.getMinutes()).padStart(2, '0');
    const ss = String(date.getSeconds()).padStart(2, '0');
    
    document.getElementById('currentTime').innerText = `${hh}:${mm}:${ss}`;
}

// Завантаження при старті
document.addEventListener('DOMContentLoaded', () => {
    updateDashboard();
    // Оновлення статусів кожні 5 секунд
    setInterval(updateDashboard, 5000);
    // Плавний хід годинника щосекунди
    setInterval(tick, 500);

    // Перемикання режимів відображення
    document.getElementById('temp-card').addEventListener('click', () => {
        fetch('/api/display/temp').catch(e => console.error(e));
    });

    document.getElementById('hum-card').addEventListener('click', () => {
        fetch('/api/display/hum').catch(e => console.error(e));
    });

    // Тогл логів
    document.getElementById('copyright').addEventListener('click', () => {
        const logsDiv = document.getElementById('logs-container');
        logsDiv.style.display = logsDiv.style.display === 'none' ? 'block' : 'none';
        if (logsDiv.style.display === 'block') {
            logsDiv.scrollTop = logsDiv.scrollHeight;
        }
    });

    // Регулювання яскравості
    const brightnessSlider = document.getElementById('brightnessSlider');
    const brightnessLabel = document.getElementById('brightnessLabel');
    
    if (brightnessSlider && brightnessLabel) {
        brightnessSlider.addEventListener('input', (e) => {
            const val = e.target.value;
            brightnessLabel.innerText = val;
        });

        brightnessSlider.addEventListener('change', (e) => {
            const val = e.target.value;
            fetch(`/api/display/brightness?level=${val}`)
                .then(r => {
                    if (!r.ok) throw new Error('Failed to set brightness');
                })
                .catch(err => console.error(err));
        });
    }
});
