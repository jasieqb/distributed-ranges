#include <CL/sycl.hpp>
#include <ranges>
#include <shp/shp.hpp>
#include <vector>

std::vector<shp::device_ptr<int>> ptrs;

template <typename T>
auto allocate_device_span(std::size_t size, std::size_t rank,
                          cl::sycl::context context, auto &&devices) {
  auto data = shp::device_allocator<T>(context, devices[rank]).allocate(size);
  ptrs.push_back(data);

  return lib::device_span<T, decltype(data)>(data, size, rank);
}

template <typename T>
auto allocate_device_spans(std::size_t size, cl::sycl::context context,
                           auto &&devices) {
  std::vector<lib::device_span<T, shp::device_ptr<T>>> spans;
  for (size_t rank = 0; rank < devices.size(); rank++) {
    spans.push_back(allocate_device_span<T>(size, rank, context, devices));
  }
  return spans;
}

template <typename T>
lib::device_span<T> allocate_shared_span(std::size_t size, std::size_t rank,
                                         auto &&devices) {
  cl::sycl::queue q(devices[rank]);
  T *data = cl::sycl::malloc_shared<T>(size, q);

  return lib::device_span<T>(data, size, rank);
}

template <typename T>
std::vector<lib::device_span<T>> allocate_shared_spans(std::size_t size,
                                                       auto &&devices) {
  std::vector<lib::device_span<T>> spans;
  for (size_t rank = 0; rank < devices.size(); rank++) {
    spans.push_back(allocate_shared_span<T>(size, rank, devices));
  }
  return spans;
}

int main(int argc, char **argv) {
  namespace sycl = cl::sycl;

  // Get GPU devices.
  sycl::gpu_selector g;
  // auto devices = shp::get_devices(g);
  auto devices = shp::get_numa_devices(g);

  shp::init(devices);

  // sycl::context context(devices);
  sycl::context context = shp::context();

  // Let's call `device[i]` rank `i`.

  printf("We have %lu devices.\n", devices.size());

  // Total number of elements
  std::size_t size = 200;

  // Elements per rank, ceil(size / devices.size())
  std::size_t size_per_segment = (size + devices.size() - 1) / devices.size();

  printf("Allocating device segments...\n");
  auto segments =
      allocate_device_spans<int>(size_per_segment, context, devices);

  printf("Device span...\n");
  lib::distributed_span dspan(segments);

  printf("Launching on segments...\n");
  size_t iteration = 0;
  for (auto&& segment : dspan.segments()) {
    printf("Segment %lu\n", iteration++);
    // sycl::queue q(context, devices[segment.rank()]);
    sycl::queue q(devices[0]);
    int *ptr = segment.begin().local();
    q.parallel_for(sycl::range<1>(segment.size()), [=](auto id) {
       ptr[id] = id;
     }).wait();
  }

  auto subspan = dspan.subspan(25, 70);

  printf("Subspan has %lu elements in it.\n", subspan.size());

  shp::print_range(subspan);
  shp::device_policy policy(devices);
  auto r_sub = shp::reduce(policy, subspan, 0.0f, std::plus());
  printf("Reduced to value %lf\n", r_sub);

  printf("Printing range...\n");
  shp::print_range(dspan);

  printf("Creating policy...\n");

  printf("Calling for_each...\n");
  shp::for_each(policy, dspan, [](auto&& elem) { elem = elem + 2; });

  shp::print_range(dspan);

  auto r = shp::reduce(policy, dspan, 0.0f, std::plus());

  /*
  shp::for_each(policy, shp::enumerate(dspan), [](auto&& elem) {
                                                 auto&& [index, value] = elem;
                                                 dspan[index] = 123;
                                               });

					       */


  return 0;
}
