function updateDashboard() {
    fetch('/status')
        .then(response => {
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
        })
        .catch(error => {
            console.error('Помилка отримання даних:', error);
            const statusBadge = document.getElementById('connection-status');
            statusBadge.innerText = 'Офлайн';
            statusBadge.classList.remove('status-online');
            statusBadge.classList.add('status-offline');
        });
}

// Завантаження при старті
document.addEventListener('DOMContentLoaded', () => {
    updateDashboard();
    // Оновлення кожні 5 секунд
    setInterval(updateDashboard, 5000);
});
