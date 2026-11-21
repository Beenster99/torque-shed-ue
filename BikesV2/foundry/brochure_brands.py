# The six makers' sales brochures: each maker's identity (logo, type, colours, strapline), its story, the
# letter that opens the brochure, and the words for every bike in its range. brochures.py lays these out.
# Everything here is written as the maker would write it. Figures come from the bike data, not from here.

F = "fonts/brochure/"


def face(family, file, weight="400", style="normal"):
    return (family, F + file, weight, style)


# ---------------------------------------------------------------------------------------------- logos
def logo_corvano(ink, accent, metal, scale=1.0, word=True):
    mark = (f'<g transform="translate(0,4)"><polygon points="4,66 20,30 46,10 74,16 100,34 72,40 60,58 66,84 40,70" fill="{ink}"/>'
            f'<polygon points="56,24 66,26 60,32" fill="{accent}"/>'
            f'<polygon points="4,66 40,70 66,84 30,92" fill="{accent}"/></g>')
    w = (f'<text x="122" y="70" font-family="Bodoni Moda" font-style="italic" font-weight="800" font-size="64" style="font-variation-settings:\'opsz\' 6" '
         f'letter-spacing="3" fill="{ink}">CORVANO</text>'
         f'<rect x="124" y="84" width="352" height="2.2" fill="{metal}"/>'
         f'<text x="124" y="102" font-family="Montserrat" font-weight="500" font-size="11.5" letter-spacing="5.2" fill="{ink}">MOTOCICLETTE · BRESCIA · 1949</text>') if word else ""
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {490 if word else 104} 110">{mark}{w}</svg>'


def logo_bsx(ink, accent, metal, scale=1.0, word=True):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 240 240">'
            f'<circle cx="120" cy="120" r="116" fill="{accent}"/><circle cx="120" cy="120" r="108" fill="none" stroke="{metal}" stroke-width="2"/>'
            f'<circle cx="120" cy="120" r="74" fill="none" stroke="{metal}" stroke-width="2"/>'
            f'<defs><path id="bsxt" d="M 120 120 m -89 0 a 89 89 0 1 1 178 0"/><path id="bsxb" d="M 120 120 m -96 0 a 96 96 0 0 0 192 0"/></defs>'
            f'<text font-family="Libre Franklin" font-weight="600" font-size="15" letter-spacing="4.5" fill="{metal}"><textPath href="#bsxt" startOffset="50%" text-anchor="middle">HARTWELL WORKS · DERBYSHIRE</textPath></text>'
            f'<text font-family="Libre Franklin" font-weight="600" font-size="15" letter-spacing="4.5" fill="{metal}"><textPath href="#bsxb" startOffset="50%" text-anchor="middle">MOTOR CYCLES SINCE 1908</textPath></text>'
            f'<text x="120" y="141" text-anchor="middle" font-family="Libre Baskerville" font-weight="700" font-size="62" letter-spacing="2" fill="{metal}">BSX</text>'
            f'<rect x="72" y="153" width="96" height="2" fill="{metal}"/><polygon points="120,62 123,71 132,71 125,76.5 127.5,85 120,80 112.5,85 115,76.5 108,71 117,71" fill="{metal}"/>'
            f'</svg>')


def logo_kazeda(ink, accent, metal, scale=1.0, word=True):
    mark = (f'<polygon points="18,8 40,8 22,102 0,102" fill="{ink}"/>'
            f'<polygon points="74,8 104,8 50,50 36,50 38,40" fill="{accent}"/>'
            f'<polygon points="34,60 50,56 92,102 62,102" fill="{ink}"/>'
            f'<polygon points="84,56 118,56 115,62 81,62" fill="{metal}"/>')
    w = (f'<text x="136" y="80" font-family="Chakra Petch" font-style="italic" font-weight="700" font-size="74" letter-spacing="6" fill="{ink}">KAZEDA</text>'
         f'<text x="140" y="102" font-family="Chakra Petch" font-weight="500" font-size="12.5" letter-spacing="6.4" fill="{ink}">KAZEDA MOTOR CO. NIIGATA</text>') if word else ""
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {470 if word else 120} 110">{mark}{w}</svg>'


def logo_varga(ink, accent, metal, scale=1.0, word=True):
    mark = (f'<polygon points="0,6 30,6 52,78 44,104 30,104" fill="{ink}"/>'
            f'<polygon points="86,6 116,6 74,104 60,104 56,90" fill="{accent}"/>')
    w = (f'<text x="128" y="96" font-family="Barlow Condensed" font-style="italic" font-weight="800" font-size="118" letter-spacing="1" fill="{ink}">VARGA</text>') if word else ""
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {400 if word else 118} 110">{mark}{w}</svg>'


def logo_lodestar(ink, accent, metal, scale=1.0, word=True):
    star = ('120,14 129,62 150,41 137,68 186,77 137,86 150,113 129,92 120,140 111,92 90,113 103,86 54,77 103,68 90,41 111,62')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 250">'
            f'<g transform="translate(120,0)"><circle cx="120" cy="77" r="72" fill="none" stroke="{ink}" stroke-width="3"/>'
            f'<circle cx="120" cy="77" r="64" fill="none" stroke="{ink}" stroke-width="1"/><polygon points="{star}" fill="{accent}"/></g>'
            f'<text x="240" y="204" text-anchor="middle" font-family="Alfa Slab One" font-size="66" letter-spacing="3" fill="{ink}">LODESTAR</text>'
            f'<rect x="52" y="216" width="92" height="2" fill="{ink}"/><rect x="336" y="216" width="92" height="2" fill="{ink}"/>'
            f'<text x="240" y="238" text-anchor="middle" font-family="Zilla Slab" font-weight="600" font-size="15" letter-spacing="5" fill="{ink}">MOTOR CO. · DULUTH, MINN. · 1919</text>'
            f'</svg>')


def logo_rheinhart(ink, accent, metal, scale=1.0, word=True):
    fins = "".join(f'<rect x="{x}" y="38" width="3" height="34" fill="{ink}"/>' for x in (14, 21, 28, 35, 82, 89, 96, 103))
    mark = (f'<polygon points="60,2 110,30 110,80 60,108 10,80 10,30" fill="none" stroke="{ink}" stroke-width="3.5"/>'
            f'{fins}<rect x="12" y="52" width="96" height="6" fill="{ink}"/><circle cx="60" cy="55" r="17" fill="{accent}"/><circle cx="60" cy="55" r="6" fill="{metal}"/>')
    w = (f'<text x="136" y="68" font-family="Jost" font-weight="500" font-size="50" letter-spacing="13" fill="{ink}">RHEINHART</text>'
         f'<text x="138" y="94" font-family="Jost" font-weight="400" font-size="12.5" letter-spacing="5.6" fill="{ink}">MOTORRADWERKE KOBLENZ · SEIT 1923</text>') if word else ""
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {560 if word else 120} 110">{mark}{w}</svg>'


# ---------------------------------------------------------------------------------------------- brands
BRANDS = {
"corvano": dict(
    name="Corvano", file="Corvano", logo=logo_corvano, logo_w=128,
    fonts=[face("Bodoni Moda", "BodoniModa_opsz_wght.ttf", "400 900"), face("Bodoni Moda", "BodoniModa-Italic_opsz_wght.ttf", "400 900", "italic"),
           face("Montserrat", "Montserrat_wght.ttf", "100 900")],
    display="'Bodoni Moda', serif", dstyle="italic", dweight="700", dcase="none", dtrack="0", dsize=1.0,
    text="'Montserrat', sans-serif", tweight="400",
    paper="#F3EFE8", ink="#16171A", accent="#B3122A", metal="#B08D57", cover="#16171A", cover_ink="#F3EFE8",
    strap="Niente di troppo.", strap_sub="Nothing you don't need.",
    year="Collezione 2027", range_title="La gamma", country="Brescia, Italia",
    signer="Elena Corvano", role="Amministratore Delegato, Corvano Motociclette S.p.A.",
    quote="My grandfather said a motorcycle is finished when you can take nothing more away.",
    letter=[
        "Dear rider,",
        "My grandfather Aldo kept a steel rule on his bench with one word scratched into it: <i>togliere</i>. Take away. He and his brother Piero opened this factory in 1949 with eleven men, a lathe bought on credit and the conviction that weight is the enemy of everything good in a motorcycle.",
        "Seventy-eight years later the rule is still on the bench, and it is still how we work. Every Corvano starts as a trellis of steel tube and a 90 degree V-twin, and then we argue, for months, about what else truly has to be there. The answer is always less than you expect. That is why our tails are short, our waists are narrow and our tube work is left in plain sight. We have nothing to hide behind a panel.",
        "In these pages you will find seven motorcycles. They do very different jobs, but put them side by side and you will see one family, drawn by the same hands in the same room above the assembly line in Brescia.",
        "Come and ride one. You will understand in the first corner.",
    ],
    history_title="Seventy-eight years of taking away",
    history=[
        ("1949", "Brothers Aldo and Piero Corvano leave the aircraft works at Brescia and build their first machine, a 125 cc single in a bicycle-thin frame. It weighs 68 kg. They sell forty in the first year."),
        ("1958", "The first Corvano V-twin, the Bicilindrica 500, wins its class in the Milano to Taranto road race, 1,400 km flat out on open roads. The 90 degree layout has been ours ever since."),
        ("1974", "Engineer Rina Balestri welds the first Corvano trellis frame. It is so much lighter than the old cradle that the race team refuses to give the prototype back."),
        ("1996", "The single-sided swingarm arrives on the road bikes, born from endurance racing where a rear wheel had to come out in eleven seconds."),
        ("2027", "Still family owned, still in Brescia. 412 people build every Corvano on one line, and each engine carries the signature of the person who assembled it."),
    ],
    why_title="Why Corvano",
    why=[
        ("The trellis", "Short, straight tubes of chrome-molybdenum steel, welded by hand and finished in bronze. Stiff where the bike steers, forgiving where the road talks back."),
        ("The V-twin", "Two cylinders at 90 degrees are in perfect primary balance. You get the punch of a twin with none of the tingle, and an engine narrow enough to sit between your knees."),
        ("The weight", "We publish the weight of every part we make. If a bracket can be 40 grams lighter, it will be, even if nobody will ever see it."),
        ("The signature", "One technician builds each engine from the first bearing to the last cover, then signs the crankcase. If you ever visit, they will want to meet you."),
    ],
    care="Every new Corvano carries a four-year, unlimited-distance warranty and four years of roadside cover across Europe. Service intervals are 12,000 km. Valve checks are at 24,000 km.",
    bikes={
        "naked": ("Lama 850", "The blade.", [
            "Lama means blade, and this is the Corvano idea at its sharpest: engine, trellis, one seat and a low horizontal lamp. Nothing else was allowed on the drawing.",
            "The V-twin sits in the open as part of the frame, so the bike is as slim at the waist as a 400. It turns on a thought and drives out of a corner on a wave of torque from barely 4,000 rpm."]),
        "sport": ("Saetta 600", "Lightning, tightly wrapped.", [
            "The Saetta is our race department's road bike. The fairing was shaped in the wind tunnel at Brescia and then cut back until the trellis showed through, because a Corvano should never hide its frame.",
            "It revs to 14,000 rpm, it changes direction like a 250 and the stubby tail carries nothing you would not want on a qualifying lap."]),
        "cafe": ("Rapida 750", "Sunday morning, eight o'clock.", [
            "Low bars, a long tank to lie along and a single humped seat. The Rapida is for the empty road out of town before the rest of the world is awake.",
            "Under the paint it is pure Corvano: bronze trellis, the 750 twin as a stressed member and a single-sided arm that shows off the whole rear wheel."]),
        "classic": ("Emilia 750", "The road it was named for.", [
            "The Via Emilia runs dead straight across the plain, then climbs into the hills. This is the Corvano for both halves of it: an upright seat, wire wheels, twin shocks and a flat bench with room for two.",
            "The softer tune of the 750 twin pulls cleanly from walking pace, and the twin low pipes give it the voice of the bikes Aldo built."]),
        "adventure": ("Valico 900", "Over the top.", [
            "Valico is the high point of a mountain pass, and the Valico 900 was signed off on the gravel of the Gavia. Long-travel suspension and a tall screen sit on the same slim trellis as our road bikes.",
            "At 900 cc it is the largest Corvano twin, built for full luggage, a passenger and thin air, yet it still weighs less than anything else in its class."]),
        "mx": ("Fango 450", "Mud is a colour.", [
            "The Fango is the only two-stroke we make, because nothing else hits as hard for the weight. A 450 single in a trellis frame with 300 mm of travel at each end.",
            "It is built in the race shop, in small numbers, by the people who look after the factory team's bikes on Sunday."]),
        "cruiser": ("Riviera 1200", "The long way along the coast.", [
            "A Corvano with its feet forward. The Riviera stretches the wheelbase to 1,640 mm, drops the seat to 690 mm and gives the big twin 1,200 cc to play with.",
            "It is still the lightest cruiser you can buy, and the only one that looks forward to the hairpins above the sea."]),
    }),

"hartwell": dict(
    name="BSX", file="BSX", logo=logo_bsx, logo_w=70,
    fonts=[face("Libre Baskerville", "LibreBaskerville_wght.ttf", "400 700"), face("Libre Baskerville", "LibreBaskerville-Italic_wght.ttf", "400 700", "italic"),
           face("Libre Franklin", "LibreFranklin_wght.ttf", "100 900")],
    display="'Libre Baskerville', serif", dstyle="normal", dweight="700", dcase="none", dtrack="-0.01em", dsize=0.86,
    text="'Libre Franklin', sans-serif", tweight="400",
    paper="#F1EBDD", ink="#14181B", accent="#1F4D3A", metal="#E9DFC4", cover="#1F4D3A", cover_ink="#F1EBDD",
    strap="Made properly.", strap_sub="Hartwell Works, Derbyshire. Since 1908.",
    year="The 2027 Catalogue", range_title="The range", country="Hartwell, Derbyshire, England",
    signer="Tom Ashdown", role="Head of Design, BSX Motor Cycles Ltd",
    quote="We draw the whole machine by hand before anybody is allowed near a computer.",
    letter=[
        "Dear rider,",
        "There is a drawing office on the top floor of the Hartwell Works with north-facing windows and a floor that still slopes towards the mill race. Every BSX since 1908 has started life up there, in pencil, on paper. I should know. I have spent nineteen years rubbing most of them out.",
        "We work this way because a pencil is honest. You cannot hide a clumsy line in a sketch, and a machine that looks right on paper tends to be right on the road. A good many of those sketches are in this catalogue, exactly as they left our boards, because we would rather show you how we think than tell you.",
        "You will notice the same things on every page. A round lamp. A tank cut in for your knees. A ribbed seat that is comfortable after three hours, not thirty minutes. These are not a style. They are simply what ninety thousand miles of testing on Derbyshire roads keeps teaching us.",
        "I hope you find a machine here that you will keep for a very long time. That is rather the point.",
    ],
    history_title="A hundred and nineteen years at Hartwell",
    history=[
        ("1908", "Blackstone, Stroud and Exley, makers of bicycle fittings, fit a 3 horsepower engine to a strengthened frame in a former cotton mill at Hartwell. The initials on the tank stay for good."),
        ("1934", "The Model T parallel twin sets a pattern that lasts ninety years: two cylinders side by side, a cradle frame and a tank shaped around the rider's knees."),
        ("1961", "A works BSX twin laps the Mountain Course at over 100 mph. The upswept twin silencers fitted for ground clearance that week have been a signature ever since."),
        ("1988", "The first BSX triple. Smooth as a four and strong as a twin, it becomes the engine of our faster machines."),
        ("2027", "The mill wheel now drives a generator that lights the drawing office. Every frame is still brazed, painted and lined by hand on the same site."),
    ],
    why_title="Why a BSX",
    why=[
        ("Drawn by hand", "Six designers, one room, a great deal of tracing paper. Every line on a BSX has been argued over by somebody holding a pencil."),
        ("Built on one site", "Frames, tanks, paint and final assembly all happen at Hartwell. If something is not right, the person who made it is a short walk away."),
        ("Twins and triples", "Parallel twins for character, triples for pace. We have made nothing else for forty years, and we have become rather good at them."),
        ("Made to be kept", "We hold parts for every machine we have built since 1946. A BSX is meant to be handed on, not traded in."),
    ],
    care="Every new BSX carries a five-year warranty with no mileage limit, and your first service at the Works is on us, with tea. Service intervals are 10,000 miles.",
    bikes={
        "naked": ("Kestrel 850", "Quick, and quietly so.", [
            "The Kestrel is the triple at its most straightforward: a round lamp, a knee-cut tank, a ribbed seat and 850 cc of the smoothest engine we make.",
            "It will potter through the village in top gear and then hover at 10,000 rpm over the moor road, which is how it came by the name."]),
        "sport": ("Merlin 600", "Small, fast and very sure of itself.", [
            "Britain's smallest falcon lends its name to our fully faired sports machine. The Merlin's 600 revs to 14,000 rpm, and its fairing was drawn around the round lamp, not the other way about.",
            "It is a proper road bike first. The bars are a sensible height, the seat is kind and the twin silencers sit high and out of the way."]),
        "cafe": ("Swift 750", "From the Works to the coast before breakfast.", [
            "Clip-on bars, rear-set pegs and a humped single seat on the 750 twin. The Swift is our nod to the lads who raced between transport cafes on the old A6.",
            "It is lighter and lower than the Hartwell it grew from, with stiffer springs and a livelier tune, but it has kept its manners."]),
        "classic": ("Hartwell 750", "The one that carries our address.", [
            "If you asked a child to draw a motorcycle, this is what you would get, and we take that as high praise. Twin shocks, wire wheels, a bench seat for two and the air-cooled look of the twin our grandfathers built.",
            "It is the easiest BSX to ride and the hardest to part with. More Hartwells are still with their first owner than any other machine we make."]),
        "adventure": ("Harrier 900", "For the far side of the hill.", [
            "The Harrier takes the triple to 900 cc and puts it in a long-travel chassis with a tall screen, hand guards and room for a fortnight's luggage.",
            "It was proved on the green lanes above the Works and on a rather longer run to the Sahara and back. Both sets of mud are still on the prototype."]),
        "chopper": ("Rook 1800", "Hartwell misbehaves.", [
            "Every so often the drawing office is allowed off the lead. The Rook is a rigid-framed, long-forked chopper with an 1,800 cc twin, a king-and-queen seat and a sissy bar as tall as a gatepost.",
            "It is built to order in the old tool room, one at a time, and no two have left the Works alike."]),
    }),

"kazeda": dict(
    name="Kazeda", file="Kazeda", logo=logo_kazeda, logo_w=120,
    fonts=[face("Chakra Petch", "ChakraPetch-Bold.ttf", "700"), face("Chakra Petch", "ChakraPetch-BoldItalic.ttf", "700", "italic"),
           face("Chakra Petch", "ChakraPetch-Medium.ttf", "500"), face("IBM Plex Sans", "IBMPlexSans_wdth_wght.ttf", "100 700")],
    display="'Chakra Petch', sans-serif", dstyle="italic", dweight="700", dcase="uppercase", dtrack="0.02em", dsize=0.92,
    text="'IBM Plex Sans', sans-serif", tweight="400",
    paper="#F2F3F4", ink="#0F1114", accent="#1C4FBF", metal="#D7E23A", cover="#0F1114", cover_ink="#F2F3F4",
    strap="Precision is a feeling.", strap_sub="風の精度",
    year="2027 Model Line-up", range_title="Line-up", country="Niigata, Japan",
    signer="Haruka Mori", role="Chief Designer, Kazeda Motor Co., Ltd.",
    quote="When every part is exactly right, the machine disappears and only the road is left.",
    letter=[
        "Dear rider,",
        "The name Kazeda is written with two characters: wind, and rice field. Our founder grew up watching the wind cross the fields outside Niigata, and he said it showed him that air has a shape. We have been drawing that shape ever since.",
        "People sometimes tell me a precise machine must be a cold one. I think the opposite is true. When the throttle answers exactly as much as your wrist asked, when the bike holds the line you chose to the width of a tyre, you stop thinking about the machine at all. What is left is the feeling. That is why we chase the last tenth of a millimetre.",
        "Our eight models share a way of building: a low, crouched nose, panels laid over one another like armour, split lamps and a sharp raised tail. Under them you will find an aluminium twin-spar frame and, most often, an inline four that will spin all day.",
        "Please ride one. Then tell us what you felt. We read every letter.",
    ],
    history_title="Seventy-five years of drawing the wind",
    history=[
        ("1952", "Tetsuo Kazeda, a maker of loom shuttles in Niigata, builds a 90 cc motorcycle to carry spare parts between mills. Mill owners ask to buy it. He makes 300 that year."),
        ("1969", "The first Kazeda inline four, a 500, appears at the Tokyo show. Four cylinders across the frame become the heart of the company."),
        ("1985", "Kazeda's aluminium twin-spar frame wins the Suzuka endurance race at its first attempt and goes into production the following spring."),
        ("2004", "The split-lamp face and layered bodywork arrive together on the Tsurugi. The design language has been refined, never replaced."),
        ("2027", "Every engine is still run for twenty minutes on a test bench at Niigata before it meets its frame. Kazeda machines are sold in 61 countries."),
    ],
    why_title="Why Kazeda",
    why=[
        ("Twin-spar aluminium", "Two straight beams from steering head to swingarm pivot. The shortest path for the load makes the stiffest, lightest frame, so that is the path we take."),
        ("The inline four", "Four small pistons instead of two large ones. It is smooth enough to stand a coin on and it sings to 14,000 rpm."),
        ("Layered surfaces", "Each panel overlaps the next with an even 3 mm gap. Air is led through the bodywork, not just around it, to cool the engine and steady the bike."),
        ("Twenty minutes", "Every engine we build is run, measured and listened to by a trained inspector before it is allowed into a frame."),
    ],
    care="Every new Kazeda carries a five-year, unlimited-distance warranty. Service intervals are 12,000 km, with valve clearances checked at 42,000 km.",
    bikes={
        "naked": ("Tsurugi 850", "The straight sword.", [
            "A tsurugi is a double-edged blade, and this is the Kazeda that cuts both ways: calm in the city, ferocious on the mountain. The four sits bare in its spars under a low, split-lamp face.",
            "With the fairing gone you see how we build. Every bracket is machined, every fastener is the same grade and nothing is there for show."]),
        "sport": ("Shiden 600", "Purple lightning.", [
            "Our supersport. The Shiden's 600 cc four makes its power at 14,000 rpm and its fairing was developed over 900 hours in the Niigata wind tunnel.",
            "The nose is as low as the rules of the road allow and the tail as high. Tuck in behind the bubble and the air simply closes behind you."]),
        "cafe": ("Tsubame 750", "The swallow.", [
            "Low, quick and light on its feet, the Tsubame takes our 750 four and dresses it in a long tank, a humped seat and clip-on bars.",
            "It is a modern Kazeda in every way that matters, from the twin-spar frame to the radial brakes. It just prefers a quieter, narrower road."]),
        "classic": ("Miyabi 750", "Quiet elegance.", [
            "Miyabi is an old word for refinement without display. This is our four in its gentlest tune, with twin shocks, a flat seat and a round lamp between two small clocks.",
            "It honours the 1969 machine that made our name, and it is built to the tolerances of today."]),
        "adventure": ("Tabi 900", "The journey is the point.", [
            "Tabi means a journey taken for its own sake. The Tabi 900 gives you a 900 cc four, long-travel suspension, a tall screen and a tank range that outlasts most riders.",
            "The layered bodywork does real work here, steering warm air away from your legs and cool air to the radiator at walking pace on a rocky climb."]),
        "mx": ("Arashi 450", "Storm.", [
            "A four-stroke single with its cylinder leant back to pull the weight to the centre. An aluminium twin-spar frame scaled down from the road bikes. 300 mm of travel.",
            "The Arashi is the machine our factory riders start on at fifteen, and many never want to give it back."]),
        "chopper": ("Oni 1800", "The demon.", [
            "Nobody expected this from Niigata, which is why we built it. A rigid gooseneck frame, a fork raked out to 45 degrees and an 1,800 cc engine dressed to look air-cooled.",
            "It is the least sensible Kazeda ever made, assembled with exactly the same care as the most sensible one."]),
        "cruiser": ("Taiga 1200", "The great river.", [
            "Long, low and unhurried. The Taiga pairs a liquid-cooled 1,200 cc parallel twin with a 690 mm seat and forward controls.",
            "It flows where other cruisers lumber, because under the deep paint is a frame as carefully calculated as any we build."]),
    }),

"varga": dict(
    name="Varga", file="Varga", logo=logo_varga, logo_w=96,
    fonts=[face("Barlow Condensed", "BarlowCondensed-ExtraBoldItalic.ttf", "800", "italic"), face("Barlow Condensed", "BarlowCondensed-SemiBold.ttf", "600"),
           face("Barlow", "Barlow-Regular.ttf", "400"), face("Barlow", "Barlow-SemiBold.ttf", "600")],
    display="'Barlow Condensed', sans-serif", dstyle="italic", dweight="800", dcase="uppercase", dtrack="0", dsize=1.16,
    text="'Barlow', sans-serif", tweight="400",
    paper="#F4F4F2", ink="#17181A", accent="#F26A1B", metal="#5B6168", cover="#F26A1B", cover_ink="#17181A",
    strap="Ride it like you mean it.", strap_sub="Győr, Hungary. Racing since 1968.",
    year="2027 Range", range_title="The range", country="Győr, Hungary",
    signer="Dóra Varga", role="Chief Executive, Varga Motor Zrt.",
    quote="We do not build motorcycles for people who want to be comfortable. We build them for people who want to be awake.",
    letter=[
        "Hello,",
        "I will be honest with you from the first line, because that is how we do things in Győr. A Varga is tall. The seat is firm. The engine answers back. If you want a motorcycle that fades into the background of your life, there are some very good ones in other brochures.",
        "My father István started this company in 1968 because he kept breaking other people's enduro bikes. He built his own, with a frame of bright steel tube you could inspect at a glance and nothing on it that could snap off in a fall. He won the national title on it. Then his rivals asked to buy one.",
        "We still build that way. The trellis is left bright so you can see every weld. The panels are flat because flat panels are light and cheap to replace. The lamp is stacked vertically so the nose stays narrow. Everything is there to help you ride harder.",
        "Four models. No passengers. Come and find out.",
    ],
    history_title="Fifty-nine years of not backing off",
    history=[
        ("1968", "István Varga, a toolmaker and enduro rider from Győr, builds his own 250 in a shed behind the railway works. He wins the Hungarian enduro championship on it the same year."),
        ("1977", "Varga machines take the first three places at the Six Days. Orders arrive from eleven countries and the shed becomes a factory."),
        ("1991", "The first Varga road bike, a 600 single in a bright trellis frame, proves that a dirt bike company can make the tarmac interesting."),
        ("2012", "The parallel twin arrives: compact enough to sit in a single's frame, strong enough to embarrass bigger engines."),
        ("2027", "Varga has 31 world titles off road and on. Every engineer in the company still races something at the weekend. It is in the contract."),
    ],
    why_title="Why Varga",
    why=[
        ("Bright trellis", "Our frames are finished clear, not painted, so a crack or a bend can never hide. It is a racing habit, and it happens to look fantastic."),
        ("Flat panels", "Sharp, flat bodywork weighs less, grips your knees better and costs less to replace when you find the limit."),
        ("Tall and narrow", "A high seat and a slim waist give you room to move and ground clearance to spare. You sit on a Varga, not in it."),
        "Ready to race|Every Varga leaves Győr with fully adjustable suspension, a quickshifter and lap timer as standard. We do not have an options list for going fast.",
    ],
    care="Every new Varga carries a four-year warranty, and it stays valid if you take your bike on track. We would be disappointed if you did not.",
    bikes={
        "naked": ("Penge 850", "The edge.", [
            "Penge means blade. An 850 cc twin in the shortest, steepest chassis we make, with a seat 840 mm off the ground and a bar wide enough to wrestle with.",
            "It lifts its front wheel in three gears and stops like it has hit something. This is the Varga for roads you already know by heart."]),
        "sport": ("Villám 600", "Flash, then thunder.", [
            "The Villám is the bike our people race at the weekend, with lights. A full fairing over the bright trellis, a 600 that screams to 14,000 rpm and a tail high enough to see daylight under.",
            "It is 1,430 mm between the wheels, the shortest in its class, and it changes direction faster than you can decide to."]),
        "adventure": ("Puszta 900", "The horizon is a suggestion.", [
            "Named for the great Hungarian plain, where the tracks run straight to the edge of the sky. A 900 cc twin, rally suspension and a tower screen on our bright trellis.",
            "It was developed by the same team that runs our desert race bikes, and it shows every time the tarmac ends."]),
        "mx": ("Farkas 450", "The wolf.", [
            "This is where Varga began and where we still measure ourselves. A 450 four-stroke single, a central-tube frame and 300 mm of travel, built to be thrown at a hillside.",
            "The Farkas is the direct descendant of István's 1968 championship bike. He still checks the first one off the line each year."]),
    }),

"lodestar": dict(
    name="Lodestar", file="Lodestar", logo=logo_lodestar, logo_w=92,
    fonts=[face("Alfa Slab One", "AlfaSlabOne-Regular.ttf", "400"), face("Zilla Slab", "ZillaSlab-Regular.ttf", "400"),
           face("Zilla Slab", "ZillaSlab-SemiBold.ttf", "600"), face("Zilla Slab", "ZillaSlab-Italic.ttf", "400", "italic")],
    display="'Alfa Slab One', serif", dstyle="normal", dweight="400", dcase="none", dtrack="0.01em", dsize=0.84,
    text="'Zilla Slab', serif", tweight="400",
    paper="#EFE6D2", ink="#101113", accent="#7A1F1F", metal="#D8C9A3", cover="#101113", cover_ink="#EFE6D2",
    strap="Follow your own star.", strap_sub="Built in Duluth, Minnesota, since 1919.",
    year="2027 Model Year", range_title="The line", country="Duluth, Minnesota, U.S.A.",
    signer="Walt Brennan", role="President, Lodestar Motor Company",
    quote="Nobody ever needed a motorcycle. That's the best thing about them.",
    letter=[
        "Friend,",
        "I started at Lodestar in 1989, sweeping the floor of the plating shop on Garfield Avenue. I have done about every job in the building since, and I'll tell you what I tell every new hire. Nobody ever needed a motorcycle. People need trucks. A motorcycle is something you want, deep down, and that means we owe you more than transportation.",
        "So we build them long, low and heavy, the way this company has since two brothers came home from France in 1919. We use a 45 degree V-twin because nothing else sounds like one. We use real steel for the fenders and real chrome over it, laid on in our own shop, because you can tell the difference with your eyes shut.",
        "There are four Lodestars in this book. One of them is waiting on you. You'll know it when you see it, and when you do, take the long way home.",
        "See you out there.",
    ],
    history_title="A hundred and eight years on Garfield Avenue",
    history=[
        ("1919", "Brothers Carl and Nils Lindqvist, back from the war, build a 45 degree V-twin in a boat shed on the Duluth waterfront and name it for the North Star they steered by."),
        ("1936", "The Lodestar Big Twin crosses the continent, New York to San Francisco, in under four days. Police departments in nine states place orders."),
        ("1953", "Staggered dual pipes and deep valanced fenders appear together on the Wayfarer. Neither has left the catalog since."),
        ("1971", "Customers start cutting up their Lodestars. We take the hint and open a custom shop inside the factory, which is still the only place a Vagabond is built."),
        ("2027", "Third building, same street. The chrome is still plated in-house and every tank is still striped by hand, by one of four people."),
    ],
    why_title="Why Lodestar",
    why=[
        ("The 45 degree twin", "Two big cylinders on a single crankpin, firing unevenly. It is not the smoothest way to build an engine. It is the one with a heartbeat."),
        ("Steel and chrome", "Fenders, tanks and covers are pressed steel, plated or painted in our own shops. Tap one with a knuckle. It rings."),
        ("Low and long", "A 690 mm seat and a wheelbase over 1,600 mm put your boots flat on the ground and keep the bike steady as a freight train."),
        ("Yours alone", "Tanks, seats, bars, pipes. Every Lodestar is built to its order sheet, and we have never built two the same on purpose."),
    ],
    care="Every new Lodestar carries a five-year warranty and lifetime cover on the chrome. Valve gear is maintenance-free. Oil changes are every 8,000 miles.",
    bikes={
        "cafe": ("Speedway 750", "The fast one in the family.", [
            "Before there were drag strips there were board tracks, and Lodestars won on them. The Speedway brings that back: a 750 twin, low bars, a humped seat and not one ounce of chrome it doesn't need.",
            "It is the lightest motorcycle we make, and the only one that asks to be leaned over."]),
        "classic": ("Pioneer 750", "Where everybody starts.", [
            "The Pioneer is a motorcycle the way your grandfather would have drawn one. A teardrop tank, a bench seat, twin shocks, wire wheels and a V-twin you can count the beats of at idle.",
            "It's easy to ride, easy to own and awful hard to sell. Most folks just buy a second Lodestar to park beside it."]),
        "cruiser": ("Wayfarer 1200", "Take the long way home.", [
            "The Wayfarer has been in our catalog since 1953 and it has never been out of the top spot. 1,200 cc, deep steel fenders, staggered duals and a seat 690 mm off the road.",
            "Set the throttle, lean back and let a couple of states go by. This is what a Lodestar is for."]),
        "chopper": ("Vagabond 1800", "No fixed address.", [
            "Built one at a time in the custom shop. A rigid frame, a fork kicked out to 43 degrees, an 1,800 cc twin and as much chrome as will stick.",
            "You pick the tank, the seat, the bars and the pipes. We'll talk you out of nothing."]),
    }),

"rheinhart": dict(
    name="Rheinhart", file="Rheinhart", logo=logo_rheinhart, logo_w=140,
    fonts=[face("Jost", "Jost_wght.ttf", "100 900")],
    display="'Jost', sans-serif", dstyle="normal", dweight="300", dcase="uppercase", dtrack="0.12em", dsize=0.78,
    text="'Jost', sans-serif", tweight="400",
    paper="#EEEFF0", ink="#15171A", accent="#1A3C7A", metal="#C43B2E", cover="#E8E9EA", cover_ink="#15171A",
    strap="Nichts ohne Grund.", strap_sub="Nothing without a reason.",
    year="Programm 2027", range_title="Das Programm", country="Koblenz, Deutschland",
    signer="Dr. Katrin Vogel", role="Head of Design, Rheinhart Motorradwerke GmbH",
    quote="If we cannot explain why a part is there, the part is removed.",
    letter=[
        "Dear rider,",
        "On the wall of our design studio in Koblenz there is a single sentence, set in small type: <i>Nichts ohne Grund</i>. Nothing without a reason. It was the working rule of Ernst Rheinhart, who founded this company in 1923, and it is the first thing we ask of every line we draw.",
        "It is why our engines are flat twins. Two cylinders lying opposite each other cancel each other's forces, sit low in the machine and stand directly in the cooling air. It is why we drive the rear wheel by shaft, which needs no adjustment and no oiling. It is why the rear frame is bolted on, so it can be replaced in an hour.",
        "You may find our motorcycles plain. We prefer the word clear. Each surface is where it is because of what lies behind it, and we believe that an object which explains itself honestly will still look right in thirty years.",
        "We invite you to examine the five models that follow as closely as you wish. They were made to be examined.",
    ],
    history_title="One hundred and four years of the flat twin",
    history=[
        ("1923", "Ernst Rheinhart, an aero-engine fitter, turns a flat twin through ninety degrees so that both cylinders stand in the wind, and couples it to a drive shaft. The layout is never changed."),
        ("1938", "A Rheinhart 500 completes 100,000 km around the Nürburgring in 77 days without an engine rebuild. The works orders its test riders to stop."),
        ("1955", "The engine itself becomes the frame. Front suspension and rear subframe bolt directly to the crankcase, saving 14 kg."),
        ("1980", "A Rheinhart flat twin crosses the Sahara first, and the long-travel boxer becomes a category of its own."),
        ("2027", "The Koblenz works builds 140 motorcycles a day. The tolerance on a crankshaft journal is four thousandths of a millimetre, as it was in 1923."),
    ],
    why_title="Why Rheinhart",
    why=[
        ("The flat twin", "Perfect balance without balance shafts. A centre of gravity below the wheel axles. Cylinders cooled by the air you ride through."),
        ("Shaft drive", "No chain to adjust, oil or replace. The drive to the rear wheel is sealed for life and inspected every 40,000 km."),
        ("Engine as frame", "The crankcase carries the suspension. There is less to build, less to flex and less to carry."),
        ("Serviceability", "The rear frame unbolts. The valve covers face outward. Most routine work on a Rheinhart needs four tools, and they are under the seat."),
    ],
    care="Every new Rheinhart carries a six-year, unlimited-distance warranty. Service intervals are 15,000 km. Parts availability is guaranteed for thirty years from the date of manufacture.",
    bikes={
        "naked": ("B 85 Kurve", "The flat twin, undressed.", [
            "Kurve means bend. The B 85 is our most direct motorcycle: an 850 cc boxer, an upright tank, a single round instrument and nothing between you and the cylinders but air.",
            "With the mass of the engine below the axles it tips into a corner with unusual ease and holds its line without correction."]),
        "cafe": ("B 75 Sprint", "A shorter distance between two points.", [
            "The B 75 Sprint lowers the bars, moves the footrests back and replaces the passenger seat with a hump. The 750 cc boxer is unchanged, because it did not need to be.",
            "The result is a compact, low and very stable sports machine with the calm of a much larger one."]),
        "classic": ("B 75 Klassik", "The original proportion.", [
            "A flat bench seat, twin rear shocks, wire wheels and the horizontal line from headlamp to tail lamp that has defined a Rheinhart since the 1950s.",
            "The B 75 Klassik is not a copy of our past. It is the same idea, built with the materials and the tolerances of 2027."]),
        "adventure": ("B 90 Fernweh", "A longing for far places.", [
            "German has a word for the ache to be somewhere distant. The B 90 Fernweh is our answer to it: a 900 cc boxer, long-travel suspension, a tall screen and mounting points for three cases.",
            "Shaft drive matters most here. After 20,000 km of dust there is still no chain to adjust."]),
        "cruiser": ("B 120 Kapitän", "Command, without hurry.", [
            "Our largest flat twin, 1,200 cc, in a long double-cradle frame with a 690 mm seat. The cylinders stand out ahead of your boots like the wings of an aircraft.",
            "It gathers speed with a turbine's smoothness and carries it without effort. This is the Rheinhart for the long, straight road."]),
    }),
}
# one "why" entry was written as "title|text": make them all pairs
for _b in BRANDS.values():
    _b["why"] = [tuple(w.split("|", 1)) if isinstance(w, str) else w for w in _b["why"]]
