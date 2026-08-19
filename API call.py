import requests

# Use the mDNS name or the IP address shown in the Serial Monitor
ESP32_URL = "http://screwdriver.local"

def api_call(mode):
    response = requests.get(f"{ESP32_URL}/api?mode={mode}", timeout=5)
    return response.json()

# Examples:
# print(api_call("info"))       # Get IP, RSSI, calibration
# print(api_call("live"))       # Get current reading
# api_call("start")             # Start recording

# api_call("stop")              # Stop recording
# data = api_call("recorded")   # Fetch all recorded data
# print(f"Got {data['count']} samples")