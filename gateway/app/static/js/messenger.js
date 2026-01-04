// Get username from localStorage (set during login)
const currentUsername = localStorage.getItem('fsx_username') || 'User';
document.getElementById('current-username').textContent = currentUsername;

// Update online users list
async function updateOnlineUsers() {
    const listEl = document.getElementById('online-users-list');
    
    try {
        const response = await fetch('/api/online');
        const data = await response.json();
        
        if (data.users && data.users.length > 0) {
            // Filter out current user
            const otherUsers = data.users.filter(u => u !== currentUsername);
            
            if (otherUsers.length === 0) {
                listEl.innerHTML = '<div class="empty-state">No other users online</div>';
                return;
            }
            
            listEl.innerHTML = otherUsers.map(username => {
                const initial = username.charAt(0).toUpperCase();
                return `
                    <div class="user-item" onclick="selectUser('${username}')">
                        <div class="user-avatar">${initial}</div>
                        <div class="user-name">${escapeHtml(username)}</div>
                        <div class="user-status"></div>
                    </div>
                `;
            }).join('');
        } else {
            listEl.innerHTML = '<div class="empty-state">No users online</div>';
        }
    } catch (error) {
        listEl.innerHTML = '<div class="empty-state">Error loading users</div>';
        console.error('Failed to load online users:', error);
    }
}

function selectUser(username) {
    // Remove active class from all items
    document.querySelectorAll('.user-item').forEach(item => {
        item.classList.remove('active');
    });
    
    // Add active class to selected item
    event.currentTarget.classList.add('active');
    
    // TODO: Load chat/file transfer interface for this user
    console.log('Selected user:', username);
}

async function handleLogout() {
    if (confirm('Are you sure you want to logout?')) {
        const username = localStorage.getItem('fsx_username');
        
        // Call logout API to close TCP connection
        if (username) {
            try {
                await fetch('/api/logout', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        username: username
                    })
                });
            } catch (error) {
                console.error('Logout API error:', error);
            }
        }
        
        localStorage.removeItem('fsx_token');
        localStorage.removeItem('fsx_username');
        window.location.href = '/login';
    }
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// Auto-refresh online users every 2 seconds
updateOnlineUsers();
setInterval(updateOnlineUsers, 2000);

