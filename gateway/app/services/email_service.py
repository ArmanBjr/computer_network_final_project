"""
Email Service for sending password reset emails
"""
import smtplib
import logging
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from typing import Optional
import os

logger = logging.getLogger(__name__)

# Email configuration from environment
SMTP_HOST = os.getenv("FSX_SMTP_HOST", "smtp.gmail.com")
SMTP_PORT = int(os.getenv("FSX_SMTP_PORT", "587"))
SMTP_USER = os.getenv("FSX_SMTP_USER", "")
SMTP_PASSWORD = os.getenv("FSX_SMTP_PASSWORD", "")
SMTP_FROM = os.getenv("FSX_SMTP_FROM", SMTP_USER)
SMTP_USE_TLS = os.getenv("FSX_SMTP_USE_TLS", "true").lower() == "true"

# Base URL for reset links
RESET_BASE_URL = os.getenv("FSX_RESET_BASE_URL", "http://localhost:8000")


class EmailService:
    """Service for sending emails"""
    
    def __init__(self):
        self.enabled = bool(SMTP_USER and SMTP_PASSWORD)
        if not self.enabled:
            logger.warning("Email service disabled: SMTP credentials not configured")
    
    def send_password_reset(self, email: str, reset_token: str, username: str) -> bool:
        """
        Send password reset email.
        
        Returns:
            True if sent successfully, False otherwise
        """
        if not self.enabled:
            logger.warning(f"EMAIL_RESET_SKIPPED (SMTP disabled) email={email} token={reset_token[:8]}... reset_url={RESET_BASE_URL}/reset-password?token={reset_token}")
            # In development, log the reset URL so user can use it manually
            print(f"\n=== PASSWORD RESET TOKEN (SMTP DISABLED) ===")
            print(f"Email: {email}")
            print(f"Reset URL: {RESET_BASE_URL}/reset-password?token={reset_token}")
            print(f"Token: {reset_token}")
            print(f"===========================================\n")
            return False
        
        reset_url = f"{RESET_BASE_URL}/reset-password?token={reset_token}"
        
        subject = "FileShareX - Password Reset Request"
        body = f"""
Hello {username},

You requested a password reset for your FileShareX account.

Click the following link to reset your password:
{reset_url}

This link will expire in 1 hour.

If you did not request this password reset, please ignore this email.

Best regards,
FileShareX Team
"""
        
        html_body = f"""
<!DOCTYPE html>
<html>
<head>
    <style>
        body {{ font-family: Arial, sans-serif; line-height: 1.6; color: #333; }}
        .container {{ max-width: 600px; margin: 0 auto; padding: 20px; }}
        .button {{ display: inline-block; padding: 12px 24px; background-color: #4EA685; color: white; text-decoration: none; border-radius: 5px; margin: 20px 0; }}
        .footer {{ margin-top: 30px; font-size: 12px; color: #666; }}
    </style>
</head>
<body>
    <div class="container">
        <h2>Password Reset Request</h2>
        <p>Hello {username},</p>
        <p>You requested a password reset for your FileShareX account.</p>
        <p>
            <a href="{reset_url}" class="button">Reset Password</a>
        </p>
        <p>Or copy and paste this link into your browser:</p>
        <p style="word-break: break-all; color: #4EA685;">{reset_url}</p>
        <p><strong>This link will expire in 1 hour.</strong></p>
        <p>If you did not request this password reset, please ignore this email.</p>
        <div class="footer">
            <p>Best regards,<br>FileShareX Team</p>
        </div>
    </div>
</body>
</html>
"""
        
        try:
            msg = MIMEMultipart('alternative')
            msg['Subject'] = subject
            msg['From'] = SMTP_FROM
            msg['To'] = email
            
            # Add both plain text and HTML
            part1 = MIMEText(body, 'plain')
            part2 = MIMEText(html_body, 'html')
            msg.attach(part1)
            msg.attach(part2)
            
            # Send email
            with smtplib.SMTP(SMTP_HOST, SMTP_PORT) as server:
                if SMTP_USE_TLS:
                    server.starttls()
                server.login(SMTP_USER, SMTP_PASSWORD)
                server.send_message(msg)
            
            logger.info(f"EMAIL_RESET_SENT email={email} token={reset_token[:8]}...")
            return True
            
        except Exception as e:
            logger.error(f"EMAIL_RESET_ERROR email={email} error={e}")
            return False


# Global email service instance
email_service = EmailService()

