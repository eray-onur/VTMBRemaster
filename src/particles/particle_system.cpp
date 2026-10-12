#include "openbl/particles/particle_system.hpp"

#include "openbl/formats/keyvalues.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace openbl::particles {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kForever = std::numeric_limits<float>::infinity();
constexpr float kMaxStep = 1.0f / 30.0f;

constexpr std::array<const char*, static_cast<std::size_t>(Key::Count)> kKeyNames{
    "size", "width", "height", "rotation", "color", "red", "green", "blue", "mask", "movealign",
    "x_speed", "y_speed", "z_speed", "elevation_speed", "radius_speed", "theta_speed", "phi_speed", "parent_speed"};
constexpr std::array<const char*, static_cast<std::size_t>(SpawnKey::Count)> kSpawnKeyNames{
    "x", "y", "z", "elevation", "radius", "theta", "phi", "red", "green", "blue", "color", "mask", "size", "width",
    "height", "rotation", "timescale", "rate", "burst"};

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

const std::string* text_ptr(const formats::ParticleBlock& b, std::string_view key) {
    for (const auto& [k, v] : b.values) {
        if (formats::iequals(k, key)) {
            return &v;
        }
    }
    return nullptr;
}

float at0(const Curve& c, float fallback, float pick = 0.5f) {
    return c ? c->at(0.0f, 1.0f, pick) : fallback;
}

} // namespace

std::string ParticleLibrary::path_of(std::string_view name) {
    // Some definitions quote names with cp1252 curly quotes (0x93, 0x94),
    // which the parser keeps as part of the name.
    while (!name.empty() && static_cast<unsigned char>(name.front()) >= 0x80) {
        name.remove_prefix(1);
    }
    while (!name.empty() && static_cast<unsigned char>(name.back()) >= 0x80) {
        name.remove_suffix(1);
    }
    std::string n = lower(name);
    std::replace(n.begin(), n.end(), '\\', '/');
    if (n.starts_with("particles/")) {
        n.erase(0, 10);
    }
    if (n.ends_with(".txt")) {
        n.resize(n.size() - 4);
    }
    return "particles/" + n + ".txt";
}

const Definition* ParticleLibrary::find(std::string_view name) {
    const std::string path = path_of(name);
    if (auto it = entries_.find(path); it != entries_.end()) {
        return it->second.definition.get();
    }
    // Insert first, so definitions that spawn themselves resolve to this entry.
    Entry& entry = entries_[path];
    auto text = reader_(path);
    if (!text) {
        return nullptr;
    }
    entry.file = std::make_unique<formats::ParticleFile>(formats::parse_particle(*text));
    entry.definition = std::make_unique<Definition>();
    Definition& def = *entry.definition;
    const formats::ParticleBlock& block = entry.file->root;
    def.block = &block;
    for (std::size_t k = 0; k < kKeyNames.size(); ++k) {
        def.curves[k] = block.curve(kKeyNames[k]);
    }
    def.fps = block.curve("fps");
    def.frames = block.curve("frames");
    def.min_frames = block.curve("min_frames");
    def.max_frames = block.curve("max_frames");
    def.loop = block.curve("loop");
    def.sprite = text_ptr(block, "sprite");
    def.precipitation = at0(block.curve("precipitation"), 0.0f) != 0;
    for (const auto& child : block.children) {
        if (!formats::iequals(child.name, "spawn")) {
            continue;
        }
        SpawnDefinition spawn;
        for (std::size_t k = 0; k < kSpawnKeyNames.size(); ++k) {
            spawn.curves[k] = child.curve(kSpawnKeyNames[k]);
        }
        def.spawns.push_back(std::move(spawn));
    }
    // Children last (they may refer back to this definition).
    std::size_t s = 0;
    for (const auto& child : block.children) {
        if (formats::iequals(child.name, "spawn")) {
            def.spawns[s++].particle = find(child.text("particle"));
        } else if (formats::iequals(child.name, "collide")) {
            // Its `spawn` and `decal` blocks name the particles started where
            // this one lands. `self` (bounce, friction, gravity) and real
            // decals (vdecal_first/last) are not handled yet.
            def.collides = true;
            for (const auto& c : child.children) {
                const std::string_view particle = c.text("particle");
                if (particle.empty()) {
                    continue;
                }
                if (formats::iequals(c.name, "spawn")) {
                    def.collide_spawn = find(particle);
                } else if (formats::iequals(c.name, "decal")) {
                    def.collide_decal = find(particle);
                }
            }
        }
    }
    return &def;
}

ParticleSystem::ParticleSystem(ParticleLibrary& library, std::string_view definition, const Vec3& origin,
                               std::uint32_t seed, const Vec3& angles)
    : library_(library), origin_(origin), rng_(seed) {
    // Source angles: yaw about z, then pitch about y (down positive), then roll about x.
    const float p = angles[0] * kPi / 180.0f, y = angles[1] * kPi / 180.0f, r = angles[2] * kPi / 180.0f;
    const float sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(r), cr = std::cos(r);
    axes_[0] = {cp * cy, cp * sy, -sp};
    axes_[1] = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp};
    axes_[2] = {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
    root_ = library_.find(definition);
    if (root_) {
        spawn(*root_, nullptr, nullptr);
    }
}

void ParticleSystem::set_placement(const SystemPlacement& placement, const ParticleWorld* world) {
    placement_ = placement;
    world_ = world;
}

float ParticleSystem::initial(const SpawnDefinition& spawn, SpawnKey key, float fallback) {
    const Curve& c = spawn.curves[static_cast<std::size_t>(key)];
    return c ? c->at(0.0f, 1.0f, std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_)) : fallback;
}

ParticleSystem::Particle* ParticleSystem::spawn(const Definition& def, const Particle* parent,
                                                const SpawnDefinition* spawn) {
    if (particles_.size() >= kMaxParticlesPerSystem) {
        return nullptr;
    }
    // Where the root's children start: anywhere in a func_particle's volume,
    // or, for precipitation, under open sky in a box around the viewer.
    std::optional<Vec3> start;
    float floor_z = -std::numeric_limits<float>::infinity();
    if (spawn && parent && parent->parent_id == 0) {
        if (placement_.volume) {
            const auto& [lo, hi] = *placement_.volume;
            start = Vec3{uniform(lo[0], hi[0]), uniform(lo[1], hi[1]), uniform(lo[2], hi[2])};
        } else if (placement_.precipitation_bounds > 0 && def.precipitation) {
            const float b = placement_.precipitation_bounds;
            start = Vec3{viewer_[0] + uniform(-b, b), viewer_[1] + uniform(-b, b), viewer_[2] + uniform(0.0f, b)};
            if (world_ && world_->open_sky && !world_->open_sky(*start)) {
                return nullptr;
            }
            // It falls until the first surface below.
            if (world_ && world_->trace) {
                if (const auto hit = world_->trace(*start, {(*start)[0], (*start)[1], (*start)[2] - 4096.0f})) {
                    floor_z = hit->point[2];
                }
            }
        }
    }
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    Particle p;
    p.def = &def;
    p.id = next_id_++;
    p.pick = uniform(rng_);
    p.fps = parent ? parent->fps : 30.0f;
    if (def.fps) {
        p.fps = std::max(def.fps->at(0, 1, p.pick), 1.0f);
    }
    // `frames` sets the span keys are spread over; the lifetime is `frames`, a
    // random length between `min_frames` (or `frames`) and `max_frames`, or
    // for ever when the particle loops.
    p.loop = at0(def.loop, 0.0f) != 0;
    p.key_frames = def.frames ? std::max(def.frames->at(0, 1, uniform(rng_)), 1.0f) : p.fps;
    p.lifetime = def.frames ? p.key_frames : kForever;
    if (def.max_frames && def.frames) {
        const float min_frames = at0(def.min_frames, -1.0f);
        const float lo = min_frames > 0 ? min_frames : p.key_frames;
        const float hi = std::max(at0(def.max_frames, lo), lo);
        p.lifetime = lo + (hi - lo) * uniform(rng_);
    }
    if (p.loop) {
        p.lifetime = kForever;
    }
    p.anchor = start ? *start : parent ? parent->position : origin_;
    p.floor_z = floor_z;
    if (parent) {
        p.parent_id = parent->id;
        p.parent_last = parent->position;
    }
    if (spawn && parent) {
        // Placement around the parent: offsets along the system's axes plus
        // world-up `elevation`, and a spherical position.
        const Vec3 xyz = to_world({initial(*spawn, SpawnKey::X, 0), initial(*spawn, SpawnKey::Y, 0),
                                   initial(*spawn, SpawnKey::Z, 0)});
        p.offset = {xyz[0], xyz[1], xyz[2] + initial(*spawn, SpawnKey::Elevation, 0)};
        p.radius = initial(*spawn, SpawnKey::Radius, 0);
        p.theta = initial(*spawn, SpawnKey::Theta, 0);
        p.phi = initial(*spawn, SpawnKey::Phi, 0);
        // The spawn block modulates its children (values at the parent's age),
        // on top of what the parent itself was given.
        p.rgb_scale = {spawn_value(*parent, *spawn, SpawnKey::Red, 255) / 255 * parent->rgb_scale[0],
                       spawn_value(*parent, *spawn, SpawnKey::Green, 255) / 255 * parent->rgb_scale[1],
                       spawn_value(*parent, *spawn, SpawnKey::Blue, 255) / 255 * parent->rgb_scale[2]};
        p.color_scale = spawn_value(*parent, *spawn, SpawnKey::Color, 255) / 255 * parent->color_scale;
        p.mask_scale = spawn_value(*parent, *spawn, SpawnKey::Mask, 255) / 255 * parent->mask_scale;
        p.size_scale = spawn_value(*parent, *spawn, SpawnKey::Size, 1) * parent->size_scale;
        p.width_scale = spawn_value(*parent, *spawn, SpawnKey::Width, 1) * parent->width_scale;
        p.height_scale = spawn_value(*parent, *spawn, SpawnKey::Height, 1) * parent->height_scale;
        p.rotation_add = spawn_value(*parent, *spawn, SpawnKey::Rotation, 0) + parent->rotation_add;
        // `timescale` stretches the children's lifetimes (2: twice as long).
        const float timescale = spawn_value(*parent, *spawn, SpawnKey::Timescale, 1);
        if (timescale > 0) {
            p.fps /= timescale;
        }
    }
    p.emitters.resize(def.spawns.size());
    // Position at spawn.
    const float t = p.theta * kPi / 180.0f, ph = p.phi * kPi / 180.0f;
    for (std::size_t k = 0; k < 3; ++k) {
        const float dir = k == 0 ? std::cos(ph) * std::cos(t) : k == 1 ? std::cos(ph) * std::sin(t) : std::sin(ph);
        p.position[k] = p.anchor[k] + p.offset[k] + p.radius * dir;
    }
    particles_.push_back(std::move(p));
    return &particles_.back();
}

float ParticleSystem::value(const Particle& p, Key key, float fallback) const {
    const Curve& c = p.def->curve(key);
    if (!c) {
        return fallback;
    }
    // Keys without times spread over `frames`; looping particles repeat them.
    const float age = p.loop ? std::fmod(p.age, p.key_frames) : p.age;
    return c->at(age, p.key_frames, p.pick);
}

float ParticleSystem::spawn_value(const Particle& parent, const SpawnDefinition& spawn, SpawnKey key,
                                  float fallback) const {
    const Curve& c = spawn.curves[static_cast<std::size_t>(key)];
    if (!c) {
        return fallback;
    }
    const float age = parent.loop ? std::fmod(parent.age, parent.key_frames) : parent.age;
    return c->at(age, parent.key_frames, parent.pick);
}

Vec3 ParticleSystem::to_world(const Vec3& v) const {
    Vec3 out{};
    for (std::size_t k = 0; k < 3; ++k) {
        out[k] = axes_[0][k] * v[0] + axes_[1][k] * v[1] + axes_[2][k] * v[2];
    }
    return out;
}

void ParticleSystem::advance(float dt) {
    while (dt > 0) {
        const float h = std::min(dt, kMaxStep);
        step(h);
        dt -= h;
    }
}

void ParticleSystem::step(float dt) {
    time_ += dt;
    index_.clear();
    for (std::size_t i = 0; i < particles_.size(); ++i) {
        index_[particles_[i].id] = i;
    }
    const std::size_t count = particles_.size(); // Particles spawned this step start next step.
    for (std::size_t i = 0; i < count; ++i) {
        Particle& p = particles_[i];
        p.age += dt * p.fps;
        if (p.age >= p.lifetime) {
            p.alive = false;
            continue;
        }
        // An effect attached to the player follows the viewer.
        if (!p.parent_id && placement_.precipitation_bounds > 0) {
            p.anchor = viewer_;
        }
        // Follow the parent by `parent_speed` of its motion.
        if (p.parent_id) {
            if (auto it = index_.find(p.parent_id); it != index_.end() && particles_[it->second].alive) {
                const Vec3& now = particles_[it->second].position;
                const float follow = value(p, Key::ParentSpeed, 0.0f);
                for (std::size_t k = 0; k < 3; ++k) {
                    p.anchor[k] += (now[k] - p.parent_last[k]) * follow;
                }
                p.parent_last = now;
            }
        }
        // Speeds are per second. x/y/z run along the system's axes; elevation is world up.
        const Vec3 v = to_world({value(p, Key::XSpeed, 0), value(p, Key::YSpeed, 0), value(p, Key::ZSpeed, 0)});
        p.offset[0] += v[0] * dt;
        p.offset[1] += v[1] * dt;
        p.offset[2] += (v[2] + value(p, Key::ElevationSpeed, 0)) * dt;
        p.radius += value(p, Key::RadiusSpeed, 0) * dt;
        p.theta += value(p, Key::ThetaSpeed, 0) * dt;
        p.phi += value(p, Key::PhiSpeed, 0) * dt;
        const float t = p.theta * kPi / 180.0f, ph = p.phi * kPi / 180.0f;
        const Vec3 dir{std::cos(ph) * std::cos(t), std::cos(ph) * std::sin(t), std::sin(ph)};
        Vec3 next{};
        for (std::size_t k = 0; k < 3; ++k) {
            next[k] = p.anchor[k] + p.offset[k] + p.radius * dir[k];
            p.velocity[k] = (next[k] - p.position[k]) / dt;
        }
        if (next[2] < p.floor_z) {
            p.alive = false;
            continue;
        }
        // Colliding particles end where they meet the world.
        if (p.def->collides && world_ && world_->trace) {
            if (const auto hit = world_->trace(p.position, next)) {
                p.alive = false;
                Particle landed = p; // spawn() may reallocate.
                landed.position = hit->point;
                if (landed.def->collide_spawn) {
                    spawn(*landed.def->collide_spawn, &landed, nullptr);
                }
                if (landed.def->collide_decal) {
                    // Lifted off the surface, against z-fighting.
                    for (std::size_t k = 0; k < 3; ++k) {
                        landed.position[k] = hit->point[k] + hit->normal[k] * 0.5f;
                    }
                    if (Particle* decal = spawn(*landed.def->collide_decal, &landed, nullptr)) {
                        decal->flat_normal = hit->normal;
                    }
                }
                continue;
            }
        }
        p.position = next;

        // Children: a burst once, then `rate` per second while this particle lives.
        const Definition& def = *particles_[i].def;
        for (std::size_t e = 0; e < def.spawns.size(); ++e) {
            const SpawnDefinition& sd = def.spawns[e];
            if (!sd.particle) {
                continue;
            }
            Emitter& em = particles_[i].emitters[e];
            int n = 0;
            if (!em.burst_done) {
                const Curve& burst = sd.curves[static_cast<std::size_t>(SpawnKey::Burst)];
                if (burst) {
                    const float pick = static_cast<float>((particles_[i].id * 7919 + e) % 1000) / 1000.0f;
                    n += static_cast<int>(std::lround(burst->at(0.0f, 1.0f, pick)));
                }
                em.burst_done = true;
            }
            if (sd.curves[static_cast<std::size_t>(SpawnKey::Rate)]) {
                em.accumulator += std::max(spawn_value(particles_[i], sd, SpawnKey::Rate, 0), 0.0f) *
                                  placement_.rate_scale * dt;
                const int whole = static_cast<int>(em.accumulator);
                em.accumulator -= static_cast<float>(whole);
                n += whole;
            }
            if (n > 0) {
                // spawn() may reallocate; copy what it needs from the parent first.
                const Particle parent = particles_[i];
                for (int k = 0; k < n; ++k) {
                    spawn(*sd.particle, &parent, &sd);
                }
            }
        }
    }
    std::erase_if(particles_, [](const Particle& p) { return !p.alive; });
}

void ParticleSystem::collect(std::vector<ParticleQuad>& out) const {
    for (const Particle& p : particles_) {
        const std::string* sprite = p.def->sprite;
        if (!sprite || sprite->empty()) {
            continue;
        }
        ParticleQuad q;
        q.position = p.position;
        const float size = value(p, Key::Size, 1.0f) * p.size_scale;
        q.width = size * value(p, Key::Width, 1.0f) * p.width_scale;
        q.height = size * value(p, Key::Height, 1.0f) * p.height_scale;
        q.rotation = value(p, Key::Rotation, 0.0f) + p.rotation_add;
        const float strength = value(p, Key::Color, 255.0f) / 255.0f * p.color_scale;
        q.color = {value(p, Key::Red, 255.0f) / 255.0f * strength * p.rgb_scale[0],
                   value(p, Key::Green, 255.0f) / 255.0f * strength * p.rgb_scale[1],
                   value(p, Key::Blue, 255.0f) / 255.0f * strength * p.rgb_scale[2]};
        q.mask = std::clamp(value(p, Key::Mask, 0.0f) / 255.0f * p.mask_scale, 0.0f, 1.0f);
        q.align = value(p, Key::MoveAlign, 0.0f) != 0.0f;
        q.velocity = p.velocity;
        q.normal = p.flat_normal;
        q.sprite = sprite;
        out.push_back(q);
    }
}

} // namespace openbl::particles
