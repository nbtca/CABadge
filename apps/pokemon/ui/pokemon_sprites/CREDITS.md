# Battle sprite sources

Eight FireRed/LeafGreen-generation 64×64 PNGs (front/back of #1, #4, #7, #25)
are sourced from https://github.com/PokeAPI/sprites . Exact revision, URLs and
SHA-256 values are recorded in sources.json. They are converted losslessly to
BGRA arrays by ../../generate_pokemon_assets.py; runtime integer scaling uses
nearest-neighbour rendering. No ROM, game audio, SD card or runtime download is used.

The Pokémon images and characters belong to Nintendo / Creatures / GAME FREAK /
The Pokémon Company. The CABadge MIT code license does not license those images.
These are supplied for this personal fan-game prototype; replace or obtain
appropriate permission for a different distribution or commercial use.

Battle implementation is original CABadge C code for a restricted four-species,
damage-only subset. Design references inspected: MonsterMesh's MIT-marked
Gen1BattleEngine.{h,cpp} and ShadowEnemyx/TamaPoke's MIT battle.{h,cpp}.
No Meshtastic/GPL firmware source or TamaPoke sprite package is incorporated.
Rule/data reference: https://github.com/smogon/pokemon-showdown (MIT).
