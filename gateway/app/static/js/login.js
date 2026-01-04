let container = document.getElementById('container');

function toggle() {
    container.classList.toggle('sign-in');
    container.classList.toggle('sign-up');
    // Clear errors when switching
    hideError('signup-error');
    hideError('signin-error');
}

function showError(elementId, message) {
    const errorEl = document.getElementById(elementId);
    errorEl.textContent = message;
    errorEl.style.display = 'block';
}

function hideError(elementId) {
    const errorEl = document.getElementById(elementId);
    errorEl.style.display = 'none';
}

function showSuccess(elementId, message) {
    const successEl = document.getElementById(elementId);
    if (!successEl) {
        // Create success message element if it doesn't exist
        const form = document.querySelector('.form.sign-up, .form.sign-in');
        const div = document.createElement('div');
        div.id = elementId;
        div.className = 'success-message';
        form.insertBefore(div, form.firstChild);
    }
    const el = document.getElementById(elementId);
    el.textContent = message;
    el.style.display = 'block';
    setTimeout(() => {
        el.style.display = 'none';
    }, 3000);
}

async function handleSignup() {
    const username = document.getElementById('signup-username').value.trim();
    const email = document.getElementById('signup-email').value.trim();
    const password = document.getElementById('signup-password').value;
    const confirm = document.getElementById('signup-confirm').value;
    const btn = document.getElementById('signup-btn');
    const errorEl = document.getElementById('signup-error');

    // Clear previous errors
    hideError('signup-error');

    // Validation
    if (!email || !email.includes('@')) {
        showError('signup-error', 'Please enter a valid email address');
        return;
    }
    if (!username || !password || !confirm) {
        showError('signup-error', 'Please fill in all fields');
        return;
    }

    if (password !== confirm) {
        showError('signup-error', 'Passwords do not match');
        return;
    }

    if (password.length < 6) {
        showError('signup-error', 'Password must be at least 6 characters');
        return;
    }

    // Disable button
    btn.disabled = true;
    btn.textContent = 'Signing up...';

    try {
        const response = await fetch('/api/register', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                username: username,
                email: email,
                password: password
            })
        });

        const data = await response.json();

        if (response.ok && data.ok) {
            showSuccess('signup-success', 'Account created successfully! Please sign in.');
            // Clear form
            document.getElementById('signup-username').value = '';
            document.getElementById('signup-password').value = '';
            document.getElementById('signup-confirm').value = '';
            // Switch to sign in after 1 second
            setTimeout(() => {
                if (!container.classList.contains('sign-in')) {
                    toggle();
                }
            }, 1000);
        } else {
            showError('signup-error', data.msg || 'Registration failed');
        }
    } catch (error) {
        showError('signup-error', 'Network error: ' + error.message);
    } finally {
        btn.disabled = false;
        btn.textContent = 'Sign up';
    }
}

async function handleSignin() {
    const username = document.getElementById('signin-username').value.trim();
    const password = document.getElementById('signin-password').value;
    const btn = document.getElementById('signin-btn');
    const errorEl = document.getElementById('signin-error');

    // Clear previous errors
    hideError('signin-error');

    // Validation
    if (!username || !password) {
        showError('signin-error', 'Please enter username and password');
        return;
    }

    // Disable button
    btn.disabled = true;
    btn.textContent = 'Signing in...';

    try {
        const response = await fetch('/api/login', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                username: username,
                password: password
            })
        });

        const data = await response.json();

        if (response.ok && data.ok) {
            // Store token (for future use)
            if (data.token) {
                localStorage.setItem('fsx_token', data.token);
                localStorage.setItem('fsx_username', data.username || username);
            }
            // Redirect to messenger
            window.location.href = '/messenger';
        } else {
            showError('signin-error', data.msg || 'Login failed');
        }
    } catch (error) {
        showError('signin-error', 'Network error: ' + error.message);
    } finally {
        btn.disabled = false;
        btn.textContent = 'Sign in';
    }
}

function showForgotPassword() {
    document.getElementById('forgot-password-modal').style.display = 'flex';
    hideError('forgot-error');
}

function closeForgotPassword() {
    document.getElementById('forgot-password-modal').style.display = 'none';
    document.getElementById('forgot-email').value = '';
    hideError('forgot-error');
}

async function handleForgotPassword() {
    const email = document.getElementById('forgot-email').value.trim();
    const errorEl = document.getElementById('forgot-error');

    hideError('forgot-error');

    if (!email) {
        showError('forgot-error', 'Please enter your email address');
        return;
    }

    // Basic email validation
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    if (!emailRegex.test(email)) {
        showError('forgot-error', 'Please enter a valid email address');
        return;
    }

    try {
        const response = await fetch('/api/forgot-password', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                email: email
            })
        });

        const data = await response.json();

        if (response.ok) {
            // Show success message
            errorEl.style.display = 'block';
            errorEl.style.backgroundColor = '#efe';
            errorEl.style.color = '#27ae60';
            errorEl.style.borderColor = '#27ae60';
            errorEl.textContent = 'If the email exists, a password reset link has been sent.';
            
            // Close modal after 3 seconds
            setTimeout(() => {
                closeForgotPassword();
            }, 3000);
        } else {
            showError('forgot-error', data.msg || 'Failed to send reset link');
        }
    } catch (error) {
        showError('forgot-error', 'Network error: ' + error.message);
    }
}

// Allow Enter key to submit forms
document.addEventListener('DOMContentLoaded', function() {
    // Sign up form
    document.getElementById('signup-confirm').addEventListener('keypress', function(e) {
        if (e.key === 'Enter') {
            handleSignup();
        }
    });

    // Sign in form
    document.getElementById('signin-password').addEventListener('keypress', function(e) {
        if (e.key === 'Enter') {
            handleSignin();
        }
    });

    // Forgot password form
    document.getElementById('forgot-email').addEventListener('keypress', function(e) {
        if (e.key === 'Enter') {
            handleForgotPassword();
        }
    });

    // Close modal when clicking outside
    document.getElementById('forgot-password-modal').addEventListener('click', function(e) {
        if (e.target === this) {
            closeForgotPassword();
        }
    });
});

// Initialize to sign-in view
setTimeout(() => {
    container.classList.add('sign-in');
}, 200);

