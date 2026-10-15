foreach(name test_tasks test_task_service)
  upgrade_test(${name} LIBRARIES upgrade_application LABELS application)
endforeach()
