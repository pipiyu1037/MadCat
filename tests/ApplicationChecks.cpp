#include "Runtime/Application.h"
#include "Passes/ForwardPass.h"
#include <cstdio>
#include <stdexcept>

int main()
{
    madcat::assets::ModelAssets assets;
    madcat::render::GeometryBatch geometry;
    geometry.vertices = {{{-.5f,-.5f,0},{1,0,0}},{{.5f,-.5f,0},{0,1,0}},{{0,.5f,0},{0,0,1}}};
    geometry.indices = {0,1,2};
    for (int failure=0; failure<4; ++failure)
    {
        madcat::runtime::ApplicationOptions options;
        options.title = "MadCat Application failure checks";
        options.graphFactory = madcat::passes::buildForwardGraph;
        if (failure == 3) {
            options.graphFactory = [](madcat::graph::Graph&, const madcat::graph::GraphicsImports&)
                -> std::expected<madcat::graph::GraphicsGraph,std::string> { return std::unexpected("Injected graph preparation failure"); };
        }
        madcat::runtime::GraphicsCallbacks callbacks;
        callbacks.frame = [&](const madcat::runtime::FrameInfo&) -> std::expected<madcat::runtime::RenderFrame,std::string> {
            if (failure == 0) { throw std::runtime_error("Injected pre-acquire callback exception"); }
            if (failure == 1) { return std::unexpected("Injected frame diagnostic"); }
            madcat::runtime::RenderFrame frame;
            frame.draws.push_back({{},0,3,0});
            return frame;
        };
        callbacks.presented = [] { throw std::runtime_error("Injected post-present callback exception"); };
        if (madcat::runtime::runGraphics(options,assets,geometry,callbacks) == 0) {
            std::fprintf(stderr,"Application check failed: injected failure %d was accepted.\n",failure);
            return 1;
        }
        std::fprintf(stderr,"Application check passed: failure %d returned after cleanup.\n",failure);
    }
    madcat::scene::Scene scene;
    madcat::runtime::ApplicationOptions options;
    options.graphFactory = madcat::passes::buildForwardGraph;
    if (madcat::runtime::run(options,scene,assets,{}) == 0) { return 1; }
    std::fprintf(stderr,"Application checks: 5 failure boundaries passed.\n");
    return 0;
}
