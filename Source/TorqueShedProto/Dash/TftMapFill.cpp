#include "TftMapFill.h"

#include "World/WorldRegion.h"

namespace TorqueTft
{
namespace
{
bool Has(const std::string& In, const char* Word) { return In.find(Word) != std::string::npos; }

std::vector<MapPt> Pts(const std::vector<TorqueWorld::P2>& Line)
{
	std::vector<MapPt> Out;
	Out.reserve(Line.size());
	for (const TorqueWorld::P2& Q : Line) Out.emplace_back(Q.X, Q.Z);
	return Out;
}

// What a patch of ground is on the map (false: not drawn).
bool AreaKindOf(const std::string& Kind, const std::string& Word, AreaKind& Out)
{
	const std::string K = Kind + " " + Word;
	if (Has(K, "water") || Has(K, "lake") || Has(K, "reservoir") || Has(K, "pond") || Has(K, "basin") || Has(K, "dock") || Has(K, "river")) Out = AreaKind::Water;
	else if (Has(K, "wood") || Has(K, "forest") || Has(K, "orchard") || Has(K, "scrub")) Out = AreaKind::Wood;
	else if (Has(K, "park") || Has(K, "grass") || Has(K, "golf") || Has(K, "garden") || Has(K, "recreation") || Has(K, "pitch") || Has(K, "cemetery") ||
		Has(K, "grave") || Has(K, "allotment") || Has(K, "green") || Has(K, "common") || Has(K, "heath") || Has(K, "meadow"))
		Out = AreaKind::Park;
	else if (Has(K, "residential") || Has(K, "retail") || Has(K, "commercial") || Has(K, "industrial") || Has(K, "urban")) Out = AreaKind::Urban;
	else return false;
	return true;
}
} // namespace

RoadKind RoadKindOf(const std::string& Class)
{
	if (Has(Class, "motorway")) return RoadKind::Motorway;
	if (Has(Class, "trunk") || Has(Class, "primary")) return RoadKind::Main;
	if (Has(Class, "secondary") || Has(Class, "tertiary")) return RoadKind::Secondary;
	if (Class == "track" || Class == "service") return RoadKind::Track;
	if (Class == "path" || Class == "footway" || Class == "bridleway" || Class == "cycleway") return RoadKind::Path;
	return RoadKind::Minor;
}

void FillMap(MapData& Out, const TorqueWorld::Region& Land)
{
	Out = MapData();
	for (const TorqueWorld::GroundShape& G : Land.Ground.Areas)
	{
		AreaKind Kind = AreaKind::Park;
		if (G.Pts.size() >= 3 && AreaKindOf(G.Kind, G.Word, Kind)) Out.AddArea(Kind, Pts(G.Pts));
	}
	for (const TorqueWorld::Stream& S : Land.Streams)
		if (S.Line.size() >= 2) Out.AddLine(S.Kind == 1 ? RoadKind::Stream : RoadKind::River, std::string(), Pts(S.Line));
	for (const TorqueWorld::GroundShape& G : Land.Ground.Lines)
		if (G.Pts.size() >= 2 && (Has(G.Kind, "rail") || Has(G.Word, "rail"))) Out.AddLine(RoadKind::Rail, std::string(), Pts(G.Pts));
	for (const TorqueWorld::FeatureLine& L : Land.Scenery.Lines)
		if (L.Line.size() >= 2 && Has(L.Kind, "rail")) Out.AddLine(RoadKind::Rail, std::string(), Pts(L.Line));
	for (const TorqueWorld::GroundShape& G : Land.Ground.Paths)
		if (G.Pts.size() >= 2) Out.AddLine(RoadKind::Path, std::string(), Pts(G.Pts));
	for (size_t E = 0; E < Land.Edges.size(); ++E)
	{
		const TorqueWorld::RoadEdge& Edge = Land.Edges[E];
		if (Edge.Line.size() < 2) continue;
		const std::string Class = Edge.Class >= 0 && size_t(Edge.Class) < Land.Classes.size() ? Land.Classes[size_t(Edge.Class)] : std::string();
		Out.AddLine(RoadKindOf(Class), Land.NameOf(int(E)), Pts(Edge.Line));
	}
	for (const TorqueWorld::Building& B : Land.Buildings)
		if (B.W > 1.0 && B.D > 1.0) Out.AddBox(AreaKind::Building, B.X, B.Z, B.W, B.D, B.Yaw);
	for (const TorqueFit::Json& Place : Land.Places)
	{
		const std::string Name = Place.Str("name");
		if (Name.empty()) continue;
		const std::string Kind = Place.Str("kind");
		Out.AddLabel(Kind == "city" || Kind == "town" ? LabelKind::Town : LabelKind::Village, Name, Place.Num("x"), Place.Num("z"));
	}
	for (const TorqueFit::Json& Poi : Land.Pois)
	{
		const std::string Kind = Poi.Str("kind");
		if (Kind == "fuel") Out.AddLabel(LabelKind::Fuel, Poi.Str("name"), Poi.Num("x"), Poi.Num("z"));
		else if (Kind == "cafe" || Kind == "pub" || Kind == "inn") Out.AddLabel(LabelKind::Cafe, Poi.Str("name"), Poi.Num("x"), Poi.Num("z"));
	}
	for (const TorqueFit::Json& Mark : Land.Landmarks)
		if (!Mark.Str("name").empty() && TorqueWorld::Region::LandmarkName(Mark.Str("name")))
			Out.AddLabel(LabelKind::Landmark, Mark.Str("name"), Mark.Num("x"), Mark.Num("z"));
	Out.Index();
}
} // namespace TorqueTft
