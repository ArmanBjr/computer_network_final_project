FROM python:3.12-slim

WORKDIR /app
COPY gateway /app/gateway

RUN pip install --no-cache-dir -U pip && \
    pip install --no-cache-dir -e /app/gateway

ENV PYTHONUNBUFFERED=1

EXPOSE 8000
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
