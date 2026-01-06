// Get token from URL
const urlParams = new URLSearchParams(window.location.search);
const resetToken = urlParams.get('token');

function showError(message) {
    const errorEl = document.getElementById('reset-error');
    if (errorEl) {
        errorEl.textContent = message;
        errorEl.style.display = 'block';
    }
    const successEl = document.getElementById('reset-success');
    if (successEl) {
        successEl.style.display = 'none';
    }
}

function showSuccess(message) {
    const successEl = document.getElementById('reset-success');
    if (successEl) {
        successEl.textContent = message;
        successEl.style.display = 'block';
    }
    const errorEl = document.getElementById('reset-error');
    if (errorEl) {
        errorEl.style.display = 'none';
    }
}

async function handleResetPassword() {
    const newPasswordEl = document.getElementById('new-password');
    const confirmPasswordEl = document.getElementById('confirm-password');
    const btn = document.getElementById('reset-btn');
    
    if (!newPasswordEl || !confirmPasswordEl || !btn) {
        showError('Form elements not found');
        return;
    }
    
    const newPassword = newPasswordEl.value;
    const confirmPassword = confirmPasswordEl.value;

    // Clear previous messages
    const errorEl = document.getElementById('reset-error');
    const successEl = document.getElementById('reset-success');
    if (errorEl) {
        errorEl.style.display = 'none';
    }
    if (successEl) {
        successEl.style.display = 'none';
    }

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

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', function() {
    // Check token and show error if missing
    if (!resetToken) {
        const errorEl = document.getElementById('reset-error');
        if (errorEl) {
            errorEl.textContent = 'Invalid reset link. Missing token.';
            errorEl.style.display = 'block';
        }
    }
    
    // Allow Enter key to submit
    const confirmPasswordInput = document.getElementById('confirm-password');
    const newPasswordInput = document.getElementById('new-password');
    
    if (confirmPasswordInput) {
        confirmPasswordInput.addEventListener('keypress', function(e) {
            if (e.key === 'Enter') {
                handleResetPassword();
            }
        });
    }
    
    if (newPasswordInput) {
        newPasswordInput.addEventListener('keypress', function(e) {
            if (e.key === 'Enter') {
                handleResetPassword();
            }
        });
    }
});

