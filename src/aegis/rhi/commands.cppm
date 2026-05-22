export module aegis.rhi:commands;
import :common;
import :image_ref;

export namespace aegis::rhi
{
struct ImageLayoutTransition
{
    ImageRef imageRef;
    ResourceState oldState;
    ResourceState newState;
};
}
