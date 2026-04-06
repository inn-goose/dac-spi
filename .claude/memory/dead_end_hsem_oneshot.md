---
name: Dead End - HSEM One-Shot Notifications
description: HSEM notifications fire once then auto-disable — must re-arm with HAL_HSEM_ActivateNotification inside callback
type: project
---

Inter-core communication worked once, then never again.

**Root cause:** HSEM notifications are one-shot. After the callback fires, notification is automatically disabled.

**How to apply:** Always re-arm inside `HAL_HSEM_FreeCallback`: `__HAL_HSEM_CLEAR_FLAG(mask); HAL_HSEM_ActivateNotification(mask);`
