from typing import Dict

SA2_BASE_ID = 882000

ZONES = [
    "Leaf Forest",
    "Hot Crater",
    "Music Plant",
    "Ice Paradise",
    "Sky Canyon",
    "Techno Base",
    "Egg Utopia"
]

CHARACTERS = ["Sonic", "Cream", "Tails", "Knuckles", "Amy"]

location_table: Dict[str, int] = {}

# Generate Location IDs for Acts 1, 2, and Boss for all 7 main zones for each character
for char_idx, char_name in enumerate(CHARACTERS):
    for zone_idx, zone_name in enumerate(ZONES):
        location_table[f"{zone_name} Act 1 - {char_name}"] = SA2_BASE_ID + (char_idx * 30) + (zone_idx * 3) + 0
        location_table[f"{zone_name} Act 2 - {char_name}"] = SA2_BASE_ID + (char_idx * 30) + (zone_idx * 3) + 1
        location_table[f"{zone_name} Boss - {char_name}"]  = SA2_BASE_ID + (char_idx * 30) + (zone_idx * 3) + 2

# Add Chaos Emerald Locations
for i in range(7):
    location_table[f"Chaos Emerald {i+1} Check"] = SA2_BASE_ID + 200 + i

def get_locations():
    return location_table
