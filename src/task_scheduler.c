/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2018 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Martin Schröder <m.schroeder2007@gmail.com>                 |
  +----------------------------------------------------------------------+
*/


#include "php.h"
#include "zend.h"
#include "zend_API.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"

#include "php_task.h"

ZEND_DECLARE_MODULE_GLOBALS(task)

zend_class_entry *concurrent_task_scheduler_ce;

static zend_object_handlers concurrent_task_scheduler_handlers;


zend_bool concurrent_task_scheduler_enqueue(concurrent_task *task)
{
	concurrent_task_scheduler *scheduler;

	zval obj;
	zval retval;

	scheduler = task->scheduler;

	if (UNEXPECTED(scheduler == NULL)) {
		return 0;
	}

	if (task->status == CONCURRENT_FIBER_STATUS_INIT) {
		task->operation = CONCURRENT_TASK_OPERATION_START;
	} else if (task->status == CONCURRENT_FIBER_STATUS_SUSPENDED) {
		task->operation = CONCURRENT_TASK_OPERATION_RESUME;
	} else {
		return 0;
	}

	if (scheduler->last == NULL) {
		scheduler->first = task;
		scheduler->last = task;
	} else {
		scheduler->last->next = task;
		scheduler->last = task;
	}

	GC_ADDREF(&task->std);

	scheduler->scheduled++;

	if (scheduler->activator && scheduler->activate && !scheduler->running) {
		scheduler->activate = 0;

		ZVAL_OBJ(&obj, &scheduler->std);

		scheduler->activator_fci.param_count = 1;
		scheduler->activator_fci.params = &obj;
		scheduler->activator_fci.retval = &retval;

		zend_call_function(&scheduler->activator_fci, &scheduler->activator_fcc);
		zval_ptr_dtor(&retval);
	}

	return 1;
}


static zend_object *concurrent_task_scheduler_object_create(zend_class_entry *ce)
{
	concurrent_task_scheduler *scheduler;

	scheduler = emalloc(sizeof(concurrent_task_scheduler));
	ZEND_SECURE_ZERO(scheduler, sizeof(concurrent_task_scheduler));

	scheduler->activate = 1;

	zend_object_std_init(&scheduler->std, ce);
	scheduler->std.handlers = &concurrent_task_scheduler_handlers;

	return &scheduler->std;
}

static void concurrent_task_scheduler_object_destroy(zend_object *object)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task *task;

	scheduler = (concurrent_task_scheduler *) object;

	while (scheduler->first != NULL) {
		task = scheduler->first;

		scheduler->scheduled--;
		scheduler->first = task->next;

		OBJ_RELEASE(&task->std);
	}

	if (scheduler->activator) {
		scheduler->activator = 0;

		zval_ptr_dtor(&scheduler->activator_fci.function_name);
	}

	if (scheduler->adapter) {
		scheduler->adapter = 0;

		zval_ptr_dtor(&scheduler->adapter_fci.function_name);
	}

	OBJ_RELEASE(&scheduler->context->std);

	zend_object_std_dtor(&scheduler->std);
}

ZEND_METHOD(TaskScheduler, __construct)
{
	concurrent_task_scheduler *scheduler;
	concurrent_context_error_handler *handler;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	zval *params;
	HashTable *table;

	scheduler = (concurrent_task_scheduler *)Z_OBJ_P(getThis());

	params = NULL;
	table = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 2)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(params)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (params != NULL && Z_TYPE_P(params) == IS_ARRAY) {
		table = Z_ARRVAL_P(params);
	}

	scheduler->context = concurrent_context_object_create(table);

	if (ZEND_NUM_ARGS() > 1) {
		handler = emalloc(sizeof(concurrent_context_error_handler));
		handler->fci = fci;
		handler->fcc = fcc;

		Z_TRY_ADDREF_P(&fci.function_name);

		scheduler->context->error_handler = handler;
	}
}

ZEND_METHOD(TaskScheduler, count)
{
	concurrent_task_scheduler *scheduler;

	ZEND_PARSE_PARAMETERS_NONE();

	scheduler = (concurrent_task_scheduler *)Z_OBJ_P(getThis());

	RETURN_LONG(scheduler->scheduled);
}

ZEND_METHOD(TaskScheduler, task)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task * task;

	zval *params;
	zval obj;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	task = concurrent_task_object_create();
	task->scheduler = scheduler;

	params = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(task->fci, task->fci_cache, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

	task->fci.no_separation = 1;

	if (params == NULL) {
		task->fci.param_count = 0;
	} else {
		zend_fcall_info_args(&task->fci, params);
	}

	Z_TRY_ADDREF_P(&task->fci.function_name);

	task->context = scheduler->context;
	GC_ADDREF(&task->context->std);

	concurrent_task_scheduler_enqueue(task);

	ZVAL_OBJ(&obj, &task->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(TaskScheduler, activator)
{
	concurrent_task_scheduler *scheduler;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(scheduler->activator_fci, scheduler->activator_fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (scheduler->activator) {
		zval_ptr_dtor(&scheduler->activator_fci.function_name);
	}

	scheduler->activator = 1;

	Z_TRY_ADDREF_P(&scheduler->activator_fci.function_name);
}

ZEND_METHOD(TaskScheduler, adapter)
{
	concurrent_task_scheduler *scheduler;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(scheduler->adapter_fci, scheduler->adapter_fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (scheduler->adapter) {
		zval_ptr_dtor(&scheduler->adapter_fci.function_name);
	}

	scheduler->adapter = 1;

	Z_TRY_ADDREF_P(&scheduler->adapter_fci.function_name);
}

ZEND_METHOD(TaskScheduler, run)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task_scheduler *prev;
	concurrent_task *task;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_NONE();

	prev = TASK_G(scheduler);
	TASK_G(scheduler) = scheduler;

	scheduler->running = 1;
	scheduler->activate = 0;

	while (scheduler->first != NULL) {
		task = scheduler->first;

		scheduler->scheduled--;
		scheduler->first = task->next;

		if (scheduler->last == task) {
			scheduler->last = NULL;
		}

		// A task scheduled for start might have been inlined, do not take action in this case.
		if (task->operation != CONCURRENT_TASK_OPERATION_NONE) {
			task->next = NULL;

			if (task->operation == CONCURRENT_TASK_OPERATION_START) {
				concurrent_task_start(task);
			} else {
				concurrent_task_continue(task);
			}

			if (UNEXPECTED(EG(exception))) {
				ZVAL_OBJ(&task->result, EG(exception));
				EG(exception) = NULL;

				task->status = CONCURRENT_FIBER_STATUS_DEAD;
				concurrent_task_notify_failure(task);
			} else {
				if (task->status == CONCURRENT_FIBER_STATUS_FINISHED) {
					concurrent_task_notify_success(task);
				} else if (task->status == CONCURRENT_FIBER_STATUS_DEAD) {
					concurrent_task_notify_failure(task);
				}
			}
		}

		OBJ_RELEASE(&task->std);
	}

	scheduler->last = NULL;

	scheduler->running = 0;
	scheduler->activate = 1;

	TASK_G(scheduler) = prev;
}

ZEND_METHOD(TaskScheduler, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task scheduler is not allowed");
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_ctor, 0, 0, 0)
	ZEND_ARG_ARRAY_INFO(0, context, 1)
	ZEND_ARG_CALLABLE_INFO(0, errorHandler, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_count, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_task, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_ARRAY_INFO(0, arguments, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_activator, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_adapter, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_run, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_wakeup, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_scheduler_functions[] = {
	ZEND_ME(TaskScheduler, __construct, arginfo_task_scheduler_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(TaskScheduler, count, arginfo_task_scheduler_count, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, task, arginfo_task_scheduler_task, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, activator, arginfo_task_scheduler_activator, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, adapter, arginfo_task_scheduler_adapter, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, run, arginfo_task_scheduler_run, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, __wakeup, arginfo_task_scheduler_wakeup, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void concurrent_task_scheduler_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\TaskScheduler", task_scheduler_functions);
	concurrent_task_scheduler_ce = zend_register_internal_class(&ce);
	concurrent_task_scheduler_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_task_scheduler_ce->create_object = concurrent_task_scheduler_object_create;
	concurrent_task_scheduler_ce->serialize = zend_class_serialize_deny;
	concurrent_task_scheduler_ce->unserialize = zend_class_unserialize_deny;

	zend_class_implements(concurrent_task_scheduler_ce, 1, zend_ce_countable);

	memcpy(&concurrent_task_scheduler_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_task_scheduler_handlers.free_obj = concurrent_task_scheduler_object_destroy;
	concurrent_task_scheduler_handlers.clone_obj = NULL;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
