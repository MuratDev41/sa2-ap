from typing import Dict

SA2_ITEM_BASE = 882500

item_table: Dict[str, int] = {
    "Unlock Cream": SA2_ITEM_BASE + 0,
    "Unlock Tails": SA2_ITEM_BASE + 1,
    "Unlock Knuckles": SA2_ITEM_BASE + 2,
    "Unlock Amy": SA2_ITEM_BASE + 3,
    "Unlock Sonic": SA2_ITEM_BASE + 4,
}

# Add Chaos Emeralds
for i in range(7):
    item_table[f"Chaos Emerald {i+1}"] = SA2_ITEM_BASE + 10 + i

# Add Zone Unlocks
zone_items = [
    "Unlock Hot Crater",
    "Unlock Music Plant",
    "Unlock Ice Paradise",
    "Unlock Sky Canyon",
    "Unlock Techno Base",
    "Unlock Egg Utopia",
    "Unlock XX",
    "Unlock Leaf Forest",
]
for idx, zone_item in enumerate(zone_items):
    item_table[zone_item] = SA2_ITEM_BASE + 20 + idx

# Filler items
item_table["10 Rings"] = SA2_ITEM_BASE + 100
item_table["1-Up"] = SA2_ITEM_BASE + 101

def get_items():
    return item_table
