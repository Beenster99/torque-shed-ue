// The New bike configurator's rules (Torque Shed's BikeShop, scripts/bike_shop.gd, the "New:
// configure your own" half of the Bike shop): a style's stock bike to start from, the parts each
// configurator slot offers on it (the ones that suit the style, or all of that kind when hardly
// any do, "none" where the slot can be empty, and whatever's there now), the tyres put right for
// each wheel, the colour groups (the wheels, the mudguards), the fabrication a configured bike
// takes beyond its style's stock one, and the order as it travels to the host (plain text, so it
// rides on a shed request's Text in co-op).
//
// Plain C++ on the layout solver (BikeLayout) so it's checked against the Godot game itself by
// SimTest/shop_test (Tools/make_godot_reference.py --shop).
#pragma once

#include "BikeLayout.h"

#include <map>
#include <string>
#include <vector>

namespace TorqueFit
{
namespace Shop
{
// BikeShop.CONFIG_SLOTS: the slots the configurator offers a choice in, in its order.
const std::vector<std::string>& ConfigSlots();
// BikeShop.NO_COLOUR: slots that don't take a colour of their own (tyres are black rubber).
bool NoColour(const std::string& Slot);
// BikeShop.COLOUR_GROUPS: "wheels", "mudguards".
const std::vector<std::string>& ColourGroups();
// BikeCopy.PAINTED: the slots the bike's paint goes on (the tank and the fairing).
bool Painted(const std::string& Slot);

// PartsDB.by_kind: the catalogue's parts of Kind in its order, wheels by size (smallest rim first,
// then the narrower rim, then by name: PartsDB.wheel_before).
std::vector<std::string> KindIds(const BikeLayout& Lay, const std::string& Kind);
// PartsDB.tyre_fit: 0 if the tyre suits the wheel's rim, 1 too wide, -1 too narrow, 2 made for
// another rim diameter (0 when either isn't a part).
int TyreFit(const BikeLayout& Lay, const std::string& Tyre, const std::string& Wheel);
// PartsDB.tyres_for: the catalogue's tyres that go on the wheel, in the catalogue's order.
std::vector<std::string> TyresFor(const BikeLayout& Lay, const std::string& Wheel);

// BikeShop.options: what Slot offers on a Style bike of Parts.
std::vector<std::string> Options(const BikeLayout& Lay, const std::string& Slot, const std::string& Style, const PartsMap& Parts);
// BikeShop.fit_tyres: a tyre that goes on each wheel (the same family, else one for the style,
// else a road tyre rather than a slick).
PartsMap FitTyres(const BikeLayout& Lay, const PartsMap& Parts, const std::string& Style);
// The configurator's start for Style (BikeShopTab._new_style): its reference bike, made whole,
// tyres put right.
PartsMap StockBuild(const BikeLayout& Lay, const std::string& Style);
// BikeShopTab.set_part: Id into Slot, the rest made whole and the tyres put right.
PartsMap SetPart(const BikeLayout& Lay, const PartsMap& Parts, const std::string& Style, const std::string& Slot, const std::string& Id);

// BikeShop.is_rear_guard: a tail unit that's a mudguard, a fender or a guard (it takes the
// mudguards' colour; a cowl or a tail tidy keeps its own).
bool IsRearGuard(const BikeLayout& Lay, const std::string& Id);
// BikeShop.colour_slots: the slots colour key Key paints on a bike of Parts (a slot by its name
// when there's a part in it and it takes a colour, or a group).
std::vector<std::string> ColourSlots(const BikeLayout& Lay, const std::string& Key, const PartsMap& Parts);

// BikeShop.fab_work: the fabrication Parts takes that Style's stock bike doesn't.
std::vector<FabJob> FabWork(const BikeLayout& Lay, const PartsMap& Parts, const std::string& Style);
// BikeShop.fab_text, without the money (the shed has none): "" when it all bolts together.
std::string FabText(const BikeLayout& Lay, const PartsMap& Parts, const std::string& Style);
// BikeRig.brief_text: "Laid out to a bobber's lines, with a cafe racer's tail."
std::string BriefText(const BikeLayout& Lay, const PartsMap& Parts);
// Sales.model_text: "750cc twin" ("Bike" with no engine).
std::string ModelText(const BikeLayout& Lay, const PartsMap& Parts);
// PartsDB.style_noun ("cafe racer", but "MX bike") and style_phrase ("a cafe racer", "an MX bike").
std::string StyleNoun(const BikeLayout& Lay, const std::string& Style);
std::string StylePhrase(const BikeLayout& Lay, const std::string& Style);

// A badge for the tank (BikeBadge): a logo (one of its LOGOS ids, "" for none) and a brand name
// (up to BrandMax letters, "" for none), each in a colour of its own, sized and moved on the tank.
struct Badge
{
	std::string Logo, Brand, Font;   // Font: one of BikeBadge.FONTS ("" plain)
	double LogoColour[3] = {0.92, 0.92, 0.9};
	double BrandColour[3] = {0.92, 0.92, 0.9};
	double LogoSize = 1.0, LogoX = 0.0, LogoY = 0.0;
	double BrandSize = 1.0, BrandX = 0.0, BrandY = 0.0;
	bool Empty() const { return Logo.empty() && Brand.empty(); }
};
constexpr int BrandMax = 16;
constexpr double BadgeSizeMin = 0.4, BadgeSizeMax = 2.5, BadgeMoveMax = 0.5;
// BikeBadge.LOGOS and FONTS: id -> name, in picker order.
const std::vector<std::pair<std::string, std::string>>& Logos();
const std::vector<std::pair<std::string, std::string>>& Fonts();
// BikeBadge.clean: a known logo or none, the brand trimmed, sizes and moves in range.
Badge CleanBadge(const Badge& In);
// A badge as text (JSON, as the tank item keeps it: BikeBadge's keys), "" for none; and back, cleaned
// (an empty badge for none or nonsense).
std::string BadgeText(const Badge& B);
Badge ParseBadge(const std::string& Text);

// A configured new bike as it's ordered: its style, its parts, the colours picked (a slot or a
// group -> linear RGB; one left out comes as the factory makes it, the tank's being the bike's
// paint), each slot's or group's finish ("gloss", "matte", "metallic", "chrome", "patina"; left out:
// as it comes), and the tank's badge.
struct Order
{
	std::string Style;
	PartsMap Parts;
	double Paint[3] = {0.78, 0.09, 0.08};
	std::map<std::string, std::vector<double>> Colours;
	std::map<std::string, std::string> Finishes;
	Badge TankBadge;
};
// As text (JSON) for the request, and back. False if it isn't an order.
std::string OrderText(const Order& O);
bool ParseOrder(const BikeLayout& Lay, const std::string& Text, Order& Out);
// The order's look on each slot it colours: slot -> {r, g, b} and slot -> finish, the groups
// spread over their slots and the tank's colour being the paint on the tank and fairing
// (BikeShop.src_of with BikeCopy.build_src).
struct SlotLook
{
	double Colour[3] = {0.5, 0.5, 0.5};
	std::string Finish;
};
std::map<std::string, SlotLook> OrderLooks(const BikeLayout& Lay, const Order& O);
} // namespace Shop
} // namespace TorqueFit
