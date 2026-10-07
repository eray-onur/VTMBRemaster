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

// Value of a curve on `block` at frame 0 with its own random pick, or `fallback`.
float initial(const formats::ParticleBlock& block, std::string_view key, float fallback, std::mt19937& rng) {
    const auto c = block.curve(key);
    if (!c) {
        return fallback;
    }
    return c->at(0.0f, 1.0f, std::uniform_real_distribution<float>(0.0f, 1.0f)(rng));
}

} // namespace

std::string ParticleLibrary::path_of(std::string_view name) {
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

const formats::ParticleBlock* ParticleLibrary::find(std::string_view name) {
    const std::string path = path_of(name);
    auto it = files_.find(path);
    if (it == files_.end()) {
        std::unique_ptr<formats::ParticleFile> file;
        if (auto text = reader_(path)) {
            file = std::make_unique<formats::ParticleFile>(formats::parse_particle(*text));
        }
        it = files_.emplace(path, std::move(file)).first;
    }
    return it->second ? &it->second->root : nullptr;
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

void ParticleSystem::spawn(const formats::ParticleBlock& def, const Particle* parent,
                           const formats::ParticleBlock* spawn_block) {
    if (particles_.size() >= kMaxParticlesPerSystem) {
        return;
    }
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    Particle p;
    p.def = &def;
    p.id = next_id_++;
    p.pick = uniform(rng_);
    p.fps = parent ? parent->fps : 30.0f;
    if (const auto fps = def.curve("fps")) {
        p.fps = std::max(fps->at(0, 1, p.pick), 1.0f);
    }
    // `frames` sets the span keys are spread over; the lifetime is `frames`, a
    // random length between `min_frames` (or `frames`) and `max_frames`, or
    // for ever when the particle loops.
    p.loop = def.curve("loop") && def.curve("loop")->at(0, 1, 0) != 0;
    const auto frames = def.curve("frames");
    p.key_frames = frames ? std::max(frames->at(0, 1, uniform(rng_)), 1.0f) : p.fps;
    p.lifetime = frames ? p.key_frames : kForever;
    if (const auto max_frames = def.curve("max_frames"); max_frames && frames) {
        const auto min_frames = def.curve("min_frames");
        const float lo = min_frames && min_frames->at(0, 1, 0) > 0 ? min_frames->at(0, 1, 0) : p.key_frames;
        const float hi = std::max(max_frames->at(0, 1, 0.5f), lo);
        p.lifetime = lo + (hi - lo) * uniform(rng_);
    }
    if (p.loop) {
        p.lifetime = kForever;
    }
    p.anchor = parent ? parent->position : origin_;
    if (parent) {
        p.parent_id = parent->id;
        p.parent_last = parent->position;
    }
    if (spawn_block && parent) {
        // Placement around the parent: offsets along the system's axes plus
        // world-up `elevation`, and a spherical position.
        const Vec3 xyz = to_world({initial(*spawn_block, "x", 0, rng_), initial(*spawn_block, "y", 0, rng_),
                                   initial(*spawn_block, "z", 0, rng_)});
        p.offset = {xyz[0], xyz[1], xyz[2] + initial(*spawn_block, "elevation", 0, rng_)};
        p.radius = initial(*spawn_block, "radius", 0, rng_);
        p.theta = initial(*spawn_block, "theta", 0, rng_);
        p.phi = initial(*spawn_block, "phi", 0, rng_);
        // The spawn block modulates its children (values at the parent's age),
        // on top of what the parent itself was given.
        p.rgb_scale = {spawn_value(*parent, *spawn_block, "red", 255) / 255 * parent->rgb_scale[0],
                       spawn_value(*parent, *spawn_block, "green", 255) / 255 * parent->rgb_scale[1],
                       spawn_value(*parent, *spawn_block, "blue", 255) / 255 * parent->rgb_scale[2]};
        p.color_scale = spawn_value(*parent, *spawn_block, "color", 255) / 255 * parent->color_scale;
        p.mask_scale = spawn_value(*parent, *spawn_block, "mask", 255) / 255 * parent->mask_scale;
        p.size_scale = spawn_value(*parent, *spawn_block, "size", 1) * parent->size_scale;
        p.width_scale = spawn_value(*parent, *spawn_block, "width", 1) * parent->width_scale;
        p.height_scale = spawn_value(*parent, *spawn_block, "height", 1) * parent->height_scale;
        p.rotation_add = spawn_value(*parent, *spawn_block, "rotation", 0) + parent->rotation_add;
        // `timescale` stretches the children's lifetimes (2: twice as long).
        const float timescale = spawn_value(*parent, *spawn_block, "timescale", 1);
        if (timescale > 0) {
            p.fps /= timescale;
        }
    }
    for (const auto& child : def.children) {
        if (formats::iequals(child.name, "spawn")) {
            p.emitters.push_back({&child, library_.find(child.text("particle")), 0.0f, false});
        }
    }
    // Position at spawn.
    const float t = p.theta * kPi / 180.0f, ph = p.phi * kPi / 180.0f;
    for (std::size_t k = 0; k < 3; ++k) {
        const float dir = k == 0 ? std::cos(ph) * std::cos(t) : k == 1 ? std::cos(ph) * std::sin(t) : std::sin(ph);
        p.position[k] = p.anchor[k] + p.offset[k] + p.radius * dir;
    }
    particles_.push_back(std::move(p));
}

float ParticleSystem::curve(const Particle& p, std::string_view key, float fallback) const {
    const auto c = p.def->curve(key);
    if (!c) {
        return fallback;
    }
    // Keys without times spread over `frames`; looping particles repeat them.
    const float age = p.loop ? std::fmod(p.age, p.key_frames) : p.age;
    return c->at(age, p.key_frames, p.pick);
}

float ParticleSystem::spawn_value(const Particle& parent, const formats::ParticleBlock& block, std::string_view key,
                                  float fallback) const {
    const auto c = block.curve(key);
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
        // Follow the parent by `parent_speed` of its motion.
        if (p.parent_id) {
            if (auto it = index_.find(p.parent_id); it != index_.end() && particles_[it->second].alive) {
                const Vec3& now = particles_[it->second].position;
                const float follow = curve(p, "parent_speed", 0.0f);
                for (std::size_t k = 0; k < 3; ++k) {
                    p.anchor[k] += (now[k] - p.parent_last[k]) * follow;
                }
                p.parent_last = now;
            }
        }
        // Speeds are per second.
        // x/y/z run along the system's axes; elevation is world up.
        const Vec3 v = to_world({curve(p, "x_speed", 0), curve(p, "y_speed", 0), curve(p, "z_speed", 0)});
        p.offset[0] += v[0] * dt;
        p.offset[1] += v[1] * dt;
        p.offset[2] += (v[2] + curve(p, "elevation_speed", 0)) * dt;
        p.radius += curve(p, "radius_speed", 0) * dt;
        p.theta += curve(p, "theta_speed", 0) * dt;
        p.phi += curve(p, "phi_speed", 0) * dt;
        const float t = p.theta * kPi / 180.0f, ph = p.phi * kPi / 180.0f;
        const Vec3 dir{std::cos(ph) * std::cos(t), std::cos(ph) * std::sin(t), std::sin(ph)};
        Vec3 next{};
        for (std::size_t k = 0; k < 3; ++k) {
            next[k] = p.anchor[k] + p.offset[k] + p.radius * dir[k];
            p.velocity[k] = (next[k] - p.position[k]) / dt;
        }
        p.position = next;

        // Children: a burst once, then `rate` per second while this particle lives.
        for (std::size_t e = 0; e < particles_[i].emitters.size(); ++e) {
            Emitter& em = particles_[i].emitters[e];
            if (!em.particle) {
                continue;
            }
            int n = 0;
            if (!em.burst_done) {
                std::mt19937 local(static_cast<std::uint32_t>(particles_[i].id * 7919 + e));
                n += static_cast<int>(std::lround(initial(*em.spawn, "burst", 0, local)));
                em.burst_done = true;
            }
            if (em.spawn->curve("rate")) {
                const Particle& self = particles_[i];
                em.accumulator += std::max(spawn_value(self, *em.spawn, "rate", 0), 0.0f) * dt;
                const int whole = static_cast<int>(em.accumulator);
                em.accumulator -= static_cast<float>(whole);
                n += whole;
            }
            for (int k = 0; k < n; ++k) {
                // spawn() may reallocate; copy what it needs from the parent first.
                const Particle parent = particles_[i];
                spawn(*em.particle, &parent, em.spawn);
            }
        }
    }
    std::erase_if(particles_, [](const Particle& p) { return !p.alive; });
}

void ParticleSystem::collect(std::vector<ParticleQuad>& out) const {
    for (const Particle& p : particles_) {
        const std::string* sprite = text_ptr(*p.def, "sprite");
        if (!sprite || sprite->empty()) {
            continue;
        }
        ParticleQuad q;
        q.position = p.position;
        const float size = curve(p, "size", 1.0f) * p.size_scale;
        q.width = size * curve(p, "width", 1.0f) * p.width_scale;
        q.height = size * curve(p, "height", 1.0f) * p.height_scale;
        q.rotation = curve(p, "rotation", 0.0f) + p.rotation_add;
        const float strength = curve(p, "color", 255.0f) / 255.0f * p.color_scale;
        q.color = {curve(p, "red", 255.0f) / 255.0f * strength * p.rgb_scale[0],
                   curve(p, "green", 255.0f) / 255.0f * strength * p.rgb_scale[1],
                   curve(p, "blue", 255.0f) / 255.0f * strength * p.rgb_scale[2]};
        q.mask = std::clamp(curve(p, "mask", 0.0f) / 255.0f * p.mask_scale, 0.0f, 1.0f);
        q.align = curve(p, "movealign", 0.0f) != 0.0f;
        q.velocity = p.velocity;
        q.sprite = sprite;
        out.push_back(q);
    }
}

} // namespace openbl::particles
