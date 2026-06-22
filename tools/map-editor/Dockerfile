FROM python:3.11-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY . /app/
EXPOSE 5050
ENV MAP_HOST=0.0.0.0 \
    MAP_PORT=5050 \
    MAP_REPO_ROOT=/repo \
    PYTHONUNBUFFERED=1
CMD ["python", "app.py"]
