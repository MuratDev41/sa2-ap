from worlds.generic.Rules import set_rule

def set_rules(world):
    player = world.player
    char_names = ["Sonic", "Cream", "Tails", "Knuckles", "Amy"]
    starting_char_idx = world.options.start_char.value
    starting_char_name = char_names[starting_char_idx]

    # Zone Requirements
    zone_names = [
        "Leaf Forest",
        "Hot Crater",
        "Music Plant",
        "Ice Paradise",
        "Sky Canyon",
        "Techno Base",
        "Egg Utopia",
    ]
    zone_requirements = {
        "Leaf Forest": "Unlock Leaf Forest",
        "Hot Crater": "Unlock Hot Crater",
        "Music Plant": "Unlock Music Plant",
        "Ice Paradise": "Unlock Ice Paradise",
        "Sky Canyon": "Unlock Sky Canyon",
        "Techno Base": "Unlock Techno Base",
        "Egg Utopia": "Unlock Egg Utopia",
    }
    
    starting_zone_idx = world.options.start_zone.value
    skip_zone = zone_names[starting_zone_idx]

    # For each location in the world
    for location in world.multiworld.get_locations(player):
        # 1. Character unlock rules
        char_rule = None
        for char_name in char_names:
            if location.name.endswith(f"- {char_name}"):
                if char_name != starting_char_name:
                    char_rule = lambda state, c=char_name: state.has(f"Unlock {c}", player)
                break

        # 2. Zone unlock rules (if lock_zones is enabled)
        zone_rule = None
        if world.options.lock_zones:
            # Check normal zone locations
            for zone_name, item_name in zone_requirements.items():
                if zone_name != skip_zone and location.name.startswith(zone_name):
                    zone_rule = lambda state, item=item_name: state.has(item, player)
                    break
            
            # Check Chaos Emerald locations
            if location.name.startswith("Chaos Emerald"):
                try:
                    emerald_num = int(location.name.split(" ")[2])
                    if emerald_num >= 1 and emerald_num <= 7:
                        zone_name = zone_names[emerald_num - 1]
                        if zone_name != skip_zone:
                            item_name = zone_requirements[zone_name]
                            zone_rule = lambda state, item=item_name: state.has(item, player)
                except (ValueError, IndexError):
                    pass

        # 3. Combine rules
        if char_rule and zone_rule:
            set_rule(location, lambda state, cr=char_rule, zr=zone_rule: cr(state) and zr(state))
        elif char_rule:
            set_rule(location, char_rule)
        elif zone_rule:
            set_rule(location, zone_rule)

