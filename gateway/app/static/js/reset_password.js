// Get token from URL
const urlParams = new URLSearchParams(window.location.search);
const resetToken = urlParams.get('token');

if (!resetToken) {
    document.getElementById('reset-error').textContent = 'Invalid reset link. Missing token.';
    document.getElementById('reset-error').style.display = 'block';
}

function showError(message) {
    const errorEl = document.getElementById('reset-error');
    errorEl.textContent = message;
    errorEl.style.display = 'block';
    document.getElementById('reset-success').style.display = 'none';
}

function showSuccess(message) {
    const successEl = document.getElementById('reset-success');
    successEl.textContent = message;
    successEl.style.display = 'block';
    document.getElementById('reset-error').style.display = 'none';
}

async function handleResetPassword() {
    const newPassword = document.getElementById('new-password').value;
    const confirmPassword = document.getElementById('confirm-password').value;
    const btn = document.getElementById('reset-btn');

    // Clear previous messages
    document.getElementById('reset-error').style.display = 'none';
    document.getElementById('reset-success').style.display = 'none';

    // Validation
    if (!newPassword || !confirmPassword) {
        showError('Please fill in all fields');
        return;
    }

    if (newPassword !== confirmPassword) {
        showError('Passwords do not match');
        return;
    }

    if (newPassword.length < 6) {
        showError('Password must be at least 6 characters');
        return;
    }

    if (!resetToken) {
        showError('Invalid reset link');
        return;
    }

    // Disable button
    btn.disabled = true;
    btn.textContent = 'Resetting...';

    try {
        const response = await fetch('/api/reset-password', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                token: resetToken,
                new_password: newPassword
            })
        });

        const data = await response.json();

        if (response.ok && data.ok) {
            showSuccess('Password reset successfully! Redirecting to login...');
            // Redirect to login after 2 seconds
            setTimeout(() => {
                window.location.href = '/login';
            }, 2000);
        } else {
            showError(data.msg || 'Failed to reset password');
        }
    } catch (error) {
        showError('Network error: ' + error.message);
    } finally {
        btn.disabled = false;
        btn.textContent = 'Reset Password';
    }
}

// Allow Enter key to submit
document.addEventListener('DOMContentLoaded', function() {
    document.getElementById('confirm-password').addEventListener('keypress', function(e) {
        if (e.key === 'Enter') {
            handleResetPassword();
        }
    });
});

