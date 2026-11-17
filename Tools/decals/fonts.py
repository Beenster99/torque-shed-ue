"""The lettering faces a typed decal can use: the tank badges' twelve (Data/Fonts/Badge) and the
decal shelf's own eight (Data/Fonts/Decal), every one under the SIL Open Font Licence (its licence
beside it in licences/). A typed word is one decal, however many letters it has."""

FONTS = [
    # the decal shelf's own: gothic, tattoo, western, sign-painter and race-number faces
    {"id": "fraktur", "file": "Decal/UnifrakturMaguntia-Book.ttf", "name": "Fraktur", "caps": False, "style": "blackletter"},
    {"id": "fraktur_bold", "file": "Decal/UnifrakturCook-Bold.ttf", "name": "Fraktur Bold", "caps": False, "style": "blackletter"},
    {"id": "rocker", "file": "Decal/NewRocker-Regular.ttf", "name": "Rocker", "caps": False, "style": "tattoo"},
    {"id": "pirate", "file": "Decal/PirataOne-Regular.ttf", "name": "Pirate", "caps": False, "style": "blackletter"},
    {"id": "western", "file": "Decal/Rye-Regular.ttf", "name": "Western", "caps": True, "style": "western"},
    {"id": "saloon", "file": "Decal/Sancreek-Regular.ttf", "name": "Saloon", "caps": False, "style": "western"},
    {"id": "signwriter", "file": "Decal/MrDafoe-Regular.ttf", "name": "Signwriter", "caps": False, "style": "script"},
    {"id": "plate", "file": "Decal/Bungee-Regular.ttf", "name": "Race plate", "caps": True, "style": "numbers"},
    # the tank badges' (Fit/BikeBadge FONTS)
    {"id": "plain", "file": "Badge/OpenSans-SemiBold.ttf", "name": "Plain", "caps": True, "style": "block"},
    {"id": "bebas", "file": "Badge/BebasNeue-Regular.ttf", "name": "Tall", "caps": True, "style": "block"},
    {"id": "racing", "file": "Badge/RacingSansOne-Regular.ttf", "name": "Racing", "caps": True, "style": "racing"},
    {"id": "russo", "file": "Badge/RussoOne-Regular.ttf", "name": "Heavy", "caps": True, "style": "block"},
    {"id": "audiowide", "file": "Badge/Audiowide-Regular.ttf", "name": "Wide", "caps": True, "style": "racing"},
    {"id": "blackops", "file": "Badge/BlackOpsOne-Regular.ttf", "name": "Stencil", "caps": True, "style": "stencil"},
    {"id": "graduate", "file": "Badge/Graduate-Regular.ttf", "name": "College", "caps": True, "style": "slab"},
    {"id": "righteous", "file": "Badge/Righteous-Regular.ttf", "name": "Retro", "caps": False, "style": "retro"},
    {"id": "playfair", "file": "Badge/PlayfairDisplay.ttf", "name": "Classic", "caps": False, "style": "serif"},
    {"id": "lobster", "file": "Badge/Lobster-Regular.ttf", "name": "Lobster", "caps": False, "style": "script"},
    {"id": "pacifico", "file": "Badge/Pacifico-Regular.ttf", "name": "Surf", "caps": False, "style": "script"},
    {"id": "kaushan", "file": "Badge/KaushanScript-Regular.ttf", "name": "Brush", "caps": False, "style": "script"},
]
