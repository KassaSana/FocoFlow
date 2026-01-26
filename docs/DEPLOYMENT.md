# Deployment

## Docker Compose (recommended)

Build and run the backend + frontend containers:

```powershell
docker compose up -d --build
```

This exposes:
- Backend API on `http://localhost:8080`
- Frontend UI on `http://localhost:5173`

### Set the API base URL for the frontend build

The React app reads `VITE_API_BASE` at build time.

```powershell
$env:VITE_API_BASE="https://your-domain.example"
docker compose up -d --build
```

### Demo stack with replay + inference

```powershell
docker compose -f docker-compose.yml -f docker-compose.demo.yml up -d --build
```

This adds:
- `replay`: publishes the bundled log over ZeroMQ
- `inference`: sends predictions to the backend

## Notes

- The C++ event engine is Windows-only; it is not part of the Docker stack.
- For production, set a public API base URL and secure the backend with auth.

## CORS

By default, the backend allows local frontend origins:
`http://localhost:*` and `http://127.0.0.1:*`.

Override with an environment variable:

```powershell
$env:NEUROFOCUS_ALLOWED_ORIGIN_PATTERNS="https://your-frontend.example"
```
