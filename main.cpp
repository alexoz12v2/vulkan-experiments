#include <iostream>

#include "avkex.h"

int main() {
  std::cout << "Hello World" << std::endl;
  avkex::VulkanApp app;
  std::vector<avkex::VulkanDeviceInfo> devs = app.getEligibleDevices();
  if (devs.empty()) {
    LOG_ERR << "No vulkan capable devices found. Crashing..." LOG_RST << std::endl;
    return 1;
  }
  LOG_LOG << "Found " << devs.size() << " Vulkan Capable GPUs. Choose first" << std::endl;
  { // ensure device dies before instance
    avkex::VulkanDevice device(app.instance(), devs[0]);
    { // ensure device users die before device
      avkex::VulkanCommandBufferManager commandBufferManager(&device);
    }
  }
}

