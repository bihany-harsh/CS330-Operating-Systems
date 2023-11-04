#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>


///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

int is_valid_mem_range(unsigned long buff, u32 count, int access_bit) 
{
	// Return Heuristics:
	// Access permission errors: -EACESS
	// Invalid addresses: -EINVAL
	// Bound errors: -EBADMEM
	// Everything correct: 0

	// 1 -> read; 2 -> write; 3 -> execute into buff permissions
	int in_mms = 0;
	int in_vm_area = 0;

	struct exec_context* PCB_CURRENT = get_current_ctx();

	if (!PCB_CURRENT) {
		return -EINVAL;
	}

	struct mm_segment* mms = PCB_CURRENT->mms;
	struct vm_area* vm_area = PCB_CURRENT->vm_area;

	for (int i = 0; i < MAX_MM_SEGS; i++) {
		if (i == MM_SEG_STACK) {
			if (buff >= mms[i].start && buff + count - 1 <= mms[i].end - 1) {
				in_mms = 1;
				if(!(access_bit & mms[i].access_flags)) { 
					return -EINVAL;
				}
				// return 0;
			} 
		} else {
			if (buff >= mms[i].start && buff + count - 1 <= mms[i].next_free - 1) {
				in_mms = 1;
				if(!(access_bit & mms[i].access_flags)) { 
					return -EINVAL;
				}
				// return 0;
			}
		}
	}

	while(vm_area != NULL) {
		if (buff >= vm_area->vm_start && buff <= vm_area->vm_end - 1) {
			in_vm_area = 1;
			switch (access_bit)
			{
			case 1: { // read
				if (!(vm_area->access_flags & 1)) {
					return -EACCES;
				}
				if (buff + count - 1 > vm_area->vm_end - 1) {
					return -EBADMEM;
				}
				break;
			}
			case 2: { // write
				if (!(vm_area->access_flags & (1<<1))) {
					return -EACCES;
				}
				if (buff + count - 1 > vm_area->vm_end - 1) {
					return -EBADMEM;
				}
				break;
			}	
			default:
				return -EACCES;
			}
		}
		vm_area = vm_area->vm_next;
	}

	if (!in_mms && !in_vm_area) {
		return -EINVAL;
	}


	return 0;
}

long trace_buffer_close(struct file *filep)
{
	if (!filep) {
		return -EINVAL;
	}
	if (filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if (!filep->trace_buffer) {
		return -EINVAL;
	}

	os_page_free(USER_REG, filep->trace_buffer->trace_memory);
	os_free(filep->trace_buffer, sizeof(struct trace_buffer_info));
	os_free(filep->fops, sizeof(struct fileops));
	os_free(filep, sizeof(filep));

	filep = NULL;

	return 0;	
}

int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	if (!filep) {
		return -EINVAL;
	}
	if (filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if (!filep->trace_buffer) {
		return -EINVAL;
	}
	if (buff == NULL) {
		return -EBADMEM;
	}
	if (is_valid_mem_range((unsigned long) buff, count, 2) != 0) {
		return -EBADMEM;
	}
	if (count < 0) {
		return -EINVAL;
	}
	if (filep->mode != O_READ && filep->mode != O_RDWR) {
		return -EINVAL;
	}

	int data_read = 0;

	while (data_read < count && filep->trace_buffer->DATA_REM_READ) {
		buff[data_read] = filep->trace_buffer->trace_memory[filep->trace_buffer->READ_OFFSET];
		data_read++;
		filep->trace_buffer->DATA_REM_READ -= 1;
		filep->trace_buffer->DATA_REM_WRITE += 1;
		filep->trace_buffer->READ_OFFSET = (filep->trace_buffer->READ_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
	}
	return data_read;
}

int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
	if (!filep) {
		return -EINVAL;
	}
	if (filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if (!filep->trace_buffer) {
		return -EINVAL;
	}
	if (buff == NULL) {
		return -EBADMEM;
	}
	if (is_valid_mem_range((unsigned long) buff, count, 1) != 0) {
		return -EBADMEM;
	}
	if (count < 0) {
		return -EINVAL;
	}
	if (filep->mode != O_WRITE && filep->mode != O_RDWR) {
		return -EINVAL;
	}

	int data_written = 0;

	while(data_written < count && filep->trace_buffer->DATA_REM_WRITE) {
		filep->trace_buffer->trace_memory[filep->trace_buffer->WRITE_OFFSET] = buff[data_written];
		data_written++;
		filep->trace_buffer->DATA_REM_WRITE -= 1;
		filep->trace_buffer->DATA_REM_READ += 1;
		filep->trace_buffer->WRITE_OFFSET = (filep->trace_buffer->WRITE_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
	}
	
    return data_written;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{

	if (mode != O_RDWR && mode != O_READ && mode != O_WRITE) {
		return -EINVAL;
	}

	struct file** file_array = current->files;
	int fd = 0;
	for (;fd < MAX_OPEN_FILES; fd++) {
		if (file_array[fd] == NULL){
			break;
		}
	}

	if (fd ==  MAX_OPEN_FILES){
		return -EINVAL;
	}
	struct file* FILE = (struct file*) os_alloc(sizeof(struct file));
	if (!FILE) {
		return -ENOMEM;
	}

	// initializing the file object
	FILE->type = TRACE_BUFFER;
	FILE->mode = mode;
	FILE->offp = 0;
	FILE->ref_count = 1;
	FILE->inode = NULL;	

	// setting up trace buffer object
	struct trace_buffer_info* TBI = (struct trace_buffer_info*) os_alloc(sizeof(struct trace_buffer_info));
	if (!TBI) {
		return -ENOMEM;
	}

	TBI->trace_memory = (char*) os_page_alloc(USER_REG);
	if(!(TBI->trace_memory)) {
		return -ENOMEM;
	}

	TBI->DATA_REM_READ = 0;
	TBI->DATA_REM_WRITE = 4096;
	TBI->mode = mode;
	TBI->READ_OFFSET = 0;
	TBI->WRITE_OFFSET = 0;

	// file object trace_buffer field
	FILE->trace_buffer = TBI;

	struct fileops* FILE_fileops = (struct fileops*)os_alloc(sizeof(struct fileops));
	if (!FILE_fileops) {
		return -ENOMEM;
	}

	FILE_fileops->close = trace_buffer_close;
	FILE_fileops->write = trace_buffer_write;
	FILE_fileops->read = trace_buffer_read;

	FILE->fops = FILE_fileops;

	current->files[fd] = FILE;

	return fd;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

int find_number_of_parameters(u64 syscall_num) { // helper function
	if (
			syscall_num == SYSCALL_GETPID || 
			syscall_num == SYSCALL_GETPPID || 
			syscall_num == SYSCALL_FORK ||
			syscall_num == SYSCALL_VFORK ||
			syscall_num == SYSCALL_CFORK ||
			syscall_num == SYSCALL_PHYS_INFO ||
			syscall_num == SYSCALL_STATS ||
			syscall_num == SYSCALL_GET_USER_P ||
			syscall_num == SYSCALL_GET_COW_F
			// syscall_num == SYSCALL_END_STRACE ||
	)	{
			return 0;
		} else if (
			syscall_num == SYSCALL_EXIT ||
			syscall_num == SYSCALL_CONFIGURE ||
			syscall_num == SYSCALL_DUMP_PTT ||
			syscall_num == SYSCALL_SLEEP ||
			syscall_num == SYSCALL_PMAP ||
			syscall_num == SYSCALL_DUP ||
			syscall_num == SYSCALL_CLOSE ||
			syscall_num == SYSCALL_TRACE_BUFFER
		) {
			return 1;
		} else if (
			syscall_num == SYSCALL_SIGNAL ||
			syscall_num == SYSCALL_EXPAND ||
			syscall_num == SYSCALL_CLONE ||
			syscall_num == SYSCALL_MUNMAP ||
			syscall_num == SYSCALL_OPEN ||
			syscall_num == SYSCALL_DUP2 ||
			// syscall_num == SYSCALL_START_STRACE ||
			syscall_num == SYSCALL_STRACE
		) {
			return 2;
		} else if (
			syscall_num == SYSCALL_MPROTECT ||
			syscall_num == SYSCALL_READ ||
			syscall_num == SYSCALL_WRITE ||
			syscall_num == SYSCALL_LSEEK ||
			syscall_num == SYSCALL_READ_STRACE ||
			syscall_num == SYSCALL_READ_FTRACE
		) {
			return 3;
		} else if (
			syscall_num == SYSCALL_MMAP ||
			syscall_num == SYSCALL_FTRACE
		) {
			return 4;
		} else {
			return -EINVAL; 		// should not happen
		}
}

int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
	if (syscall_num == SYSCALL_START_STRACE || syscall_num == SYSCALL_END_STRACE) {
		return 0;
	}
	struct exec_context* current = get_current_ctx();
	if (current->st_md_base == NULL) {
		return 0;
	}
	if (current->st_md_base->is_traced == 0) {
		return 0; 		// NO tracing allowed
	}
	if (current->files[current->st_md_base->strace_fd] == NULL) {
		return 0;
	}
	if (current->files[current->st_md_base->strace_fd]->type != TRACE_BUFFER) {
		return 0;
	}

	if (syscall_num < SYSCALL_EXIT) {	// syscall_num handling
		return 0;
	} else if (syscall_num > SYSCALL_GETPID && syscall_num < SYSCALL_EXPAND) {
		return 0;
	} else if (syscall_num > SYSCALL_WRITE && syscall_num < SYSCALL_DUP) {
		return 0;
	} else if (syscall_num > SYSCALL_LSEEK && syscall_num < SYSCALL_FTRACE) {
		return 0;
	} else if (syscall_num > SYSCALL_READ_FTRACE && syscall_num < SYSCALL_GETPPID) {
		return 0;
	} else if (syscall_num > SYSCALL_GETPPID) {
		return 0;
	}

	int argc = find_number_of_parameters(syscall_num);
	u64 argv[5] = {syscall_num, param1, param2, param3, param4};

	struct file* filep = current->files[current->st_md_base->strace_fd]; // file which has trace_buffer
	if (current->st_md_base->tracing_mode == FULL_TRACING) {
		for (int i = 0; i <= argc; i++) {
			// u32 bytes_written = trace_buffer_write(filep, (char*)(&argv[i]), sizeof(u64));
			u32 bytes_written = 0;
			while (bytes_written < sizeof(u64) && filep->trace_buffer->DATA_REM_WRITE) {
				filep->trace_buffer->trace_memory[filep->trace_buffer->WRITE_OFFSET] = ((char*)(&argv[i]))[bytes_written];
				bytes_written++;
				filep->trace_buffer->DATA_REM_WRITE -= 1;
				filep->trace_buffer->DATA_REM_READ += 1;
				filep->trace_buffer->WRITE_OFFSET = (filep->trace_buffer->WRITE_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
			}
			if (bytes_written != sizeof(u64)) {
				return 0;		// should not happen
			}
		}
	} else if (current->st_md_base->tracing_mode == FILTERED_TRACING) {
		struct strace_info* iter = current->st_md_base->next;
		while(iter != NULL) {
			if (iter->syscall_num == syscall_num) {
				for (int i = 0; i <= argc; i++) {
					// u32 bytes_written = trace_buffer_write(filep, (char*)(&argv[i]), sizeof(u64));
					u32 bytes_written = 0;
					while (bytes_written < sizeof(u64) && filep->trace_buffer->DATA_REM_WRITE) {
						filep->trace_buffer->trace_memory[filep->trace_buffer->WRITE_OFFSET] = ((char*)(&argv[i]))[bytes_written];
						bytes_written++;
						filep->trace_buffer->DATA_REM_WRITE -= 1;
						filep->trace_buffer->DATA_REM_READ += 1;
						filep->trace_buffer->WRITE_OFFSET = (filep->trace_buffer->WRITE_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
					}
					if (bytes_written != sizeof(u64)) {
						return 0;		// should not happen
					}
				}
			}
			iter = iter->next;
		}
	} else {
		return 0;		// should not happen
	}
    return 0;
}

int sys_strace(struct exec_context *current, int syscall_num, int action)
{
	// Assumption 1: current->st_md_base->count is the length 
	// of the traced list (FILTERED_TRACING mode matters)

	// TODO: to check for process legitimacy (for eg. CHILD process is illegitimate) [ON HOLD]

	if (!current) {
		return -EINVAL;
	}
	if (action != ADD_STRACE && action != REMOVE_STRACE) {
		return -EINVAL;
	}
	if (syscall_num == SYSCALL_START_STRACE || syscall_num == SYSCALL_END_STRACE) {
		return -EINVAL;
	}
	if (current->st_md_base == NULL) {
		// **
		struct strace_head* new_head = os_alloc(sizeof(struct strace_head));

		if (!new_head) {
			return -EINVAL;
		}

		new_head->count = 0;
		new_head->is_traced = 0;
		new_head->last = NULL;
		new_head->next = NULL;
		new_head->strace_fd = -EINVAL;
		new_head->tracing_mode = -EINVAL;

		current->st_md_base = new_head; 	
	}	
	// if (current->st_md_base->is_traced == -EINVAL) {	// this happens when after start_strace(), we 
	// 													// we have called an end_strace() {no de-allocation}
	// 													// and only the is_traced attribute is flipped to INVALID value
	// 	return -EINVAL;
	// }

	if (current->st_md_base->count >= STRACE_MAX && action == ADD_STRACE) {
		return -EINVAL;
	}

	if (syscall_num < SYSCALL_EXIT) {	// syscall_num handling
		return -EINVAL;
	} else if (syscall_num > SYSCALL_GETPID && syscall_num < SYSCALL_EXPAND) {
		return -EINVAL;
	} else if (syscall_num > SYSCALL_WRITE && syscall_num < SYSCALL_DUP) {
		return -EINVAL;
	} else if (syscall_num > SYSCALL_LSEEK && syscall_num < SYSCALL_FTRACE) {
		return -EINVAL;
	} else if (syscall_num > SYSCALL_READ_FTRACE && syscall_num < SYSCALL_GETPPID) {
		return -EINVAL;
	} else if (syscall_num > SYSCALL_GETPPID) {
		return -EINVAL;
	}

	if (current->st_md_base->tracing_mode == FULL_TRACING) {
		if (action == REMOVE_STRACE) {
			return -EINVAL;
		}
		if (action == ADD_STRACE) {
			return 0;
		}
	}

	struct strace_info* STRACEABLE_LIST = current->st_md_base->next;

	// TODO: check thoroughly!
	if (action == ADD_STRACE) {		// if already in list ERROR
		while(STRACEABLE_LIST != NULL) {
			if (STRACEABLE_LIST->syscall_num == syscall_num) {
				return -EINVAL;
			}
			STRACEABLE_LIST = STRACEABLE_LIST->next;
		}
		struct strace_info* new_strace = os_alloc(sizeof(struct strace_info));
		if(!new_strace) {
			return -EINVAL;
		}
		new_strace->syscall_num = syscall_num;
		new_strace->next = NULL;

		if (current->st_md_base->next == NULL) {	// is list is empty
			current->st_md_base->next = new_strace;
			// current->st_md_base->last = new_strace;
		} else {
			current->st_md_base->last->next = new_strace;
		}

		current->st_md_base->last = new_strace;
		current->st_md_base->count += 1;
	} else {					// if removing something not existing -> ERROR
		struct strace_info* prev = NULL;
		while(STRACEABLE_LIST != NULL) {
			if (STRACEABLE_LIST->syscall_num == syscall_num) {
				if (prev != NULL) {
					prev->next = STRACEABLE_LIST->next;
				} else {
					current->st_md_base->next = STRACEABLE_LIST->next;
				}
				if (STRACEABLE_LIST->next == NULL) {
					current->st_md_base->last = prev;
				}
				STRACEABLE_LIST->next = NULL;
				os_free(STRACEABLE_LIST, sizeof(struct strace_info));
				current->st_md_base->count -= 1;
				return 0;
			}
			prev = STRACEABLE_LIST;
			STRACEABLE_LIST = STRACEABLE_LIST->next;
		}
		return -EINVAL;
	}

	return 0;
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	// TODO: if count > number of existing syscalls, then fill all and return no error ***
	if (filep == NULL) {
		return -EINVAL;
	}
	if (filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if (filep->trace_buffer->trace_memory == NULL) {
		return -EINVAL;
	}

	int total_bytes_read = 0;
	if (count < 0) {
		return -EINVAL;
	}
	while (count--) {
		int bytes_read = trace_buffer_read(filep, (buff + total_bytes_read), sizeof(u64));
		if (bytes_read != sizeof(u64)) {
			// return -EINVAL;
			// ***
			return total_bytes_read;
		}
		u64 syscall_num = *(u64*)(buff + total_bytes_read);
		total_bytes_read += bytes_read;
		int argc = find_number_of_parameters(syscall_num);
		while (argc--) {
			int bytes_read = trace_buffer_read(filep, (buff + total_bytes_read), sizeof(u64));
			if (bytes_read != sizeof(u64)) {
				// if syscall is found, then it is important, that all its arguments be present
				return -EINVAL;
			}
			total_bytes_read += bytes_read;
		}
	}
	return total_bytes_read;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	if (!current) {
		return -EINVAL;
	}
	if (fd < 0) {
		return -EINVAL;
	}
	if (current->files[fd] == NULL || current->files[fd]->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if (tracing_mode != FULL_TRACING && tracing_mode != FILTERED_TRACING) {
		return -EINVAL;
	}
	if (current->st_md_base == NULL) {	// when start_strace() is called for the first time.
										// In other cases, we have current->st_md_base already assigned
										// which needs re-initialization.
		struct strace_head* STRACE_HEAD = (struct strace_head*)os_alloc(sizeof(struct strace_head));

		// TODO: add meta-data of parent pid [ON HOLD]

		if (!STRACE_HEAD) {
			return -EINVAL;
		}
		current->st_md_base = STRACE_HEAD;
		current->st_md_base->last = NULL;
		current->st_md_base->next = NULL;
	}
	current->st_md_base->count = 0;
	current->st_md_base->is_traced = 1; 	// Tracing mode active
	current->st_md_base->strace_fd = fd;
	current->st_md_base->tracing_mode = tracing_mode;

	return 0;
}

int sys_end_strace(struct exec_context *current)
{
	// TODO: to check whether legitimate process is calling it (for eg. child process is illegitimate) [ON HOLD]

	if (!current) {
		return -EINVAL;
	}
	if (current->st_md_base == NULL) {	// if st_md_base not present -> errorneous
										// start_strace() was never called
		return -EINVAL;
	}
	if (current->files[current->st_md_base->strace_fd] == NULL) { // if file is not present -> errorneous
		return -EINVAL;
	}
	if (current->files[current->st_md_base->strace_fd]->type != TRACE_BUFFER) {
		return -EINVAL;
	}

	struct strace_info* precursor = current->st_md_base->next;
	struct strace_info* iterator = NULL;
	if (precursor != NULL) {
		iterator = precursor->next;
	}

	while(precursor != NULL) {
		os_free(precursor, sizeof(struct strace_info));
		precursor = iterator;
		if (iterator != NULL) {
			iterator = iterator->next;
		}
	}

	// instead of freeing the current->st_md_base,
	// we make all its properties redundant and keep the structure
	current->st_md_base->count = 0;
	current->st_md_base->is_traced = 0;
	current->st_md_base->strace_fd = -EINVAL;
	current->st_md_base->tracing_mode = -EINVAL;
	current->st_md_base->next = NULL;
	current->st_md_base->last = NULL;

	return 0;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
	if (ctx == NULL) {
		return -EINVAL;
	}
	if (ctx->files[fd_trace_buffer] == NULL) {
		return -EINVAL;
	}
	if (fd_trace_buffer < 0) {
		return -EINVAL;
	}
	if (ctx->files[fd_trace_buffer]->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	
	if (action == ADD_FTRACE) {

		if (ctx->ft_md_base == NULL) {
			struct ftrace_head* FTRACE_HEAD = os_alloc(sizeof(struct ftrace_head));

			if (!FTRACE_HEAD) {
				return -EINVAL;
			}

			FTRACE_HEAD->count = 0;
			FTRACE_HEAD->last = NULL;
			FTRACE_HEAD->next = NULL;

			ctx->ft_md_base = FTRACE_HEAD;
		}
		if (ctx->ft_md_base->count >= FTRACE_MAX) {
			return -EINVAL;
		}
		struct ftrace_info* ftrace_list = ctx->ft_md_base->next;
		while(ftrace_list != NULL) {
			if (ftrace_list->faddr == faddr) {
				return -EINVAL;
			}
			ftrace_list = ftrace_list->next;
		}
		ftrace_list = os_alloc(sizeof(struct ftrace_info));

		if (ftrace_list == NULL) {
			return -EINVAL;
		}

		ftrace_list->faddr = faddr;

		ftrace_list->code_backup[0] = INV_OPCODE;
		ftrace_list->code_backup[1] = INV_OPCODE;
		ftrace_list->code_backup[2] = INV_OPCODE;
		ftrace_list->code_backup[3] = INV_OPCODE;

		ftrace_list->num_args = nargs;
		ftrace_list->fd = fd_trace_buffer;
		ftrace_list->capture_backtrace = 0;
		ftrace_list->next = NULL;

		// Adding it to the list
		if (ctx->ft_md_base->last == NULL) {
			ctx->ft_md_base->next = ftrace_list;
			ctx->ft_md_base->last = ftrace_list;
		} else {
			ctx->ft_md_base->last->next = ftrace_list;
			ctx->ft_md_base->last = ftrace_list;
		}

		ctx->ft_md_base->count += 1;

	} else if (action == ENABLE_FTRACE) {

		if (ctx->ft_md_base == NULL) {
			return -EINVAL;
		}

		struct ftrace_info* ftrace_list = ctx->ft_md_base->next;
		int found_flag = 0;
		while(ftrace_list != NULL) {
			if (ftrace_list->faddr == faddr) {
				if (ftrace_list->code_backup[0] != INV_OPCODE) {	// already enabled
					return 0;
				}
				found_flag = 1;
				break;
			}
			ftrace_list = ftrace_list->next;
		}
		if (found_flag == 0) {		// the function was not added.
			return -EINVAL;
		}

		u8* IP = (u8*)faddr; 		// instruction that needs to be modified


		for(int i = 0; i < 4; i++) {
		// for (int i = 0; i < 2; i++) { 
			ftrace_list->code_backup[i] = *(IP + i);	// saving the code (code-backup)
			*(IP + i) = INV_OPCODE;					// invalidating the opcode
		}


	} else if (action == DISABLE_FTRACE) {

		if (ctx->ft_md_base == NULL) {
			return -EINVAL;
		}

		struct ftrace_info* ftrace_list = ctx->ft_md_base->next;
		int found_flag = 0;
		while(ftrace_list != NULL) {
			if (ftrace_list->faddr == faddr) {
				if (ftrace_list->code_backup[0] == INV_OPCODE) {	// already disabled
					return 0;
				}
				found_flag = 1;
				break;
			}
			ftrace_list = ftrace_list->next;
		}
		if (found_flag == 0) {		// the function was not added.
			return -EINVAL;
		}

		u8* IP = (u8*)faddr; 		// instruction that needs to be modified

		//TODO: here as well!
		for(int i = 0; i < 4; i++) {
		// for (int i = 0; i < 2; i++) {
			*(IP + i) = ftrace_list->code_backup[i];		// restoring the code
			ftrace_list->code_backup[i] = INV_OPCODE;			// disabling
		}

	} else if (action == REMOVE_FTRACE) {

		if (ctx->ft_md_base == NULL) {
			return -EINVAL;
		}
		int flag_found = 0;
		struct ftrace_info* ftrace_list = ctx->ft_md_base->next;
		while(ftrace_list != NULL) {
			if (ftrace_list->faddr = faddr) {
				flag_found = 1;
				break;
			}
			ftrace_list = ftrace_list->next;
		}
		if (flag_found == 0) {
			return -EINVAL;
		}

		if (ftrace_list->code_backup[0] != INV_OPCODE) {	// tracing was enabled for it
			u8* IP = (u8*)faddr;

			// TODO: here as well!
			for (int i = 0; i < 4; i++) {
			// for (int i = 0; i < 2; i++) {
				*(IP + i) = ftrace_list->code_backup[i];		// restoring the code
				ftrace_list->code_backup[i] = INV_OPCODE;			// disabling
			}
		}

		ftrace_list = ctx->ft_md_base->next;
		struct ftrace_info* prev = NULL;

		while(ftrace_list != NULL) {
			if (ftrace_list->faddr == faddr) {
				if (prev != NULL) {
					prev->next = ftrace_list->next;
				} else {
					ctx->ft_md_base->next = ftrace_list->next;
				}
				if (ftrace_list->next = NULL) {
					ctx->ft_md_base->last = prev;
				}
				ftrace_list->next = NULL;
				os_free(ftrace_list, sizeof(struct ftrace_info));
				ctx->ft_md_base->count -= 1;
				break;
			}
		}

	} else if (action == ENABLE_BACKTRACE) {

		if (ctx->ft_md_base == NULL) {
			return -EINVAL;
		}

		struct ftrace_info* ftrace_list = ctx->ft_md_base->next;
		int found_flag = 0;
		while(ftrace_list != NULL) {
			if (ftrace_list->faddr == faddr) {
				// if (ftrace_list->code_backup[0] != INV_OPCODE) {	// already enabled
				// 	return 0;
				// }
				found_flag = 1;
				break;
			}
			ftrace_list = ftrace_list->next;
		}
		if (found_flag == 0) {		// the function was not added.
			return -EINVAL;
		}

		if (ftrace_list->code_backup[0] != INV_OPCODE) {	// already enabled
			ftrace_list->capture_backtrace = 1;
			return 0;
		} else {						// if tracing is not enabled->enable it
			u8* IP = (u8*)faddr; 		// instruction that needs to be modified

			for (int i = 0; i < 4; i++) {
			// for (int i = 0; i < 2; i++) {
				ftrace_list->code_backup[i] = *(IP + i);	// saving the code (code-backup)
				*(IP + i) = INV_OPCODE;					// invalidating the opcode
			}
		}

		ftrace_list->capture_backtrace = 1;		// capture_backtrace flag!


	} else if (action == DISABLE_BACKTRACE) {

		if (ctx->ft_md_base == NULL) {
			return -EINVAL;
		}

		struct ftrace_info* ftrace_list = ctx->ft_md_base->next;
		int found_flag = 0;
		while(ftrace_list != NULL) {
			if (ftrace_list->faddr == faddr) {
				// if (ftrace_list->code_backup[0] == INV_OPCODE) {	// already disabled
				// 	return 0;
				// }
				found_flag = 1;
				break;
			}
			ftrace_list = ftrace_list->next;
		}
		if (found_flag == 0) {		// the function was not added.
			return -EINVAL;
		}

		if (ftrace_list->code_backup[0] == INV_OPCODE) {
			ftrace_list->capture_backtrace = 0;
			return 0;
		} else {
			u8* IP = (u8*)faddr; 		// instruction that needs to be modified

			for (int i = 0; i < 4; i++) {
			// for (int i = 0; i < 2; i++) {
				*(IP + i) = ftrace_list->code_backup[i];		// restoring the code
				ftrace_list->code_backup[i] = INV_OPCODE;			// disabling
			}
		}
		ftrace_list->capture_backtrace = 0;		// capture_backtrace flag!

	} else {
		// should not happen [incorrect action sequence]
		return -EINVAL;
	}

    return 0;
}

//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{	
	struct exec_context* current = get_current_ctx();

	struct ftrace_head* ft_md_base = current->ft_md_base;
	if (ft_md_base == NULL) {
		return -EINVAL;
	}
	struct ftrace_info* ftrace_list = ft_md_base->next;

	while(ftrace_list != NULL) {
		if (ftrace_list->faddr == regs->entry_rip) {
			break;
		}
		ftrace_list = ftrace_list->next;
	}

	if (ftrace_list == NULL) {
		return -EINVAL;
	}

	int argc = ftrace_list->num_args;
	u64 argv[7] = {regs->entry_rip, regs->rdi, regs->rsi, regs->rdx, regs->rcx, regs->r8, regs->r9};

	if (ftrace_list->fd < 0 || current->files[ftrace_list->fd] == NULL) {
		return -EINVAL;
	}
	struct file* filep = current->files[ftrace_list->fd];
	if (filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}

	for (int i = 0; i <= argc; i++) {
		u32 bytes_written = 0;
		while(bytes_written < sizeof(u64) && filep->trace_buffer->DATA_REM_WRITE) {
			filep->trace_buffer->trace_memory[filep->trace_buffer->WRITE_OFFSET] = ((char*)(&argv[i]))[bytes_written];
			bytes_written++;
			filep->trace_buffer->DATA_REM_READ += 1;
			filep->trace_buffer->DATA_REM_WRITE -= 1;
			filep->trace_buffer->WRITE_OFFSET = (filep->trace_buffer->WRITE_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
		}
		if (bytes_written != sizeof(u64)) {
			return -EINVAL;
		}
	}



	// address of first instruction
	// return address to subsequent calls until main-ra
	if (ftrace_list->capture_backtrace == 1) {
		u32 bytes_written = 0;
		while(bytes_written < sizeof(u64) && filep->trace_buffer->DATA_REM_WRITE) {
			filep->trace_buffer->trace_memory[filep->trace_buffer->WRITE_OFFSET] = ((char*)(&argv[0]))[bytes_written];	// address of first instruction
			bytes_written++;
			filep->trace_buffer->DATA_REM_READ += 1;
			filep->trace_buffer->DATA_REM_WRITE -= 1;
			filep->trace_buffer->WRITE_OFFSET = (filep->trace_buffer->WRITE_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
		}
		if (bytes_written != sizeof(u64)) {
			return -EINVAL;
		}

		// return address
		u64 return_address = *((u64*)regs->entry_rsp);		// return address pushed onto the stack
		u64 base_pointer = regs->rbp;

		while (return_address != END_ADDR) {
			bytes_written = 0;
			while(bytes_written < sizeof(u64) && filep->trace_buffer->DATA_REM_WRITE) {
				filep->trace_buffer->trace_memory[filep->trace_buffer->WRITE_OFFSET] = ((char*)&return_address)[bytes_written];
				bytes_written++;
				filep->trace_buffer->DATA_REM_READ += 1;
				filep->trace_buffer->DATA_REM_WRITE -= 1;
				filep->trace_buffer->WRITE_OFFSET = (filep->trace_buffer->WRITE_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
			}
			if (bytes_written != sizeof(u64)) {
				return -EINVAL;
			}
			return_address = *((u64*)base_pointer + 1);
			base_pointer = *((u64*)base_pointer);
		}	

		// pushing END_ADDR in the trace_buffer

		bytes_written = 0;
		while(bytes_written < sizeof(u64) && filep->trace_buffer->DATA_REM_WRITE) {
			filep->trace_buffer->trace_memory[filep->trace_buffer->WRITE_OFFSET] = ((char*)&return_address)[bytes_written];
			bytes_written++;
			filep->trace_buffer->DATA_REM_READ += 1;
			filep->trace_buffer->DATA_REM_WRITE -= 1;
			filep->trace_buffer->WRITE_OFFSET = (filep->trace_buffer->WRITE_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
		}
		if (bytes_written != sizeof(u64)) {
			return -EINVAL;
		}
	}

	// instead of correcting the code segment we can make the instruction execute
	// the first instruction being: "push   %rbp" [instruction len: 1 byte]
	regs->entry_rsp -= 8;							// updating the stack pointer
	*((u64*) regs->entry_rsp) = regs->rbp;			

	// second instruction is "mov    %rsp,%rbp" [instruction len: 3 byte]
	regs->rbp = regs->entry_rsp;

	// update the entry->rip as it returns to same location [1 + 3 = 4]
	regs->entry_rip += 4;

	/*
		OBJDUMP

		23:	55                   	push   %rbp
		24:	48 89 e5             	mov    %rsp,%rbp
		27:	48 83 ec 20          	sub    $0x20,%rsp
	*/

	return 0;
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
	if (filep == NULL) {
		return -EINVAL;
	}
	if (filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	struct exec_context *ctx = get_current_ctx();

	if (count < 0) {
		return -EINVAL;
	}

	if (ctx == NULL) {
		return -EINVAL;
	}
	if (ctx->ft_md_base == NULL) {
		return -EINVAL;
	}
	int total_bytes_read = 0;

	struct ftrace_head* ft_md_base = ctx->ft_md_base;
	if (ft_md_base == NULL) {
		return -EINVAL;
	}
	struct ftrace_info* ftrace_list = ft_md_base->next;

	while(count--){
		int bytes_read = trace_buffer_read(filep, (buff + total_bytes_read), sizeof(u64));
		if (bytes_read != sizeof(u64)) {
			// return -EINVAL;
			// ***
			return total_bytes_read;
		}
		unsigned long faddress = *(u64*)(buff + total_bytes_read);
		total_bytes_read += bytes_read;
		int found = 0;
		while(ftrace_list != NULL) {
			if (ftrace_list->faddr == faddress) {
				found = 1;
				break;
			}
			ftrace_list = ftrace_list->next;
		}
		if (!found) {
			return -EINVAL; 
			// should not happen
		}
		int argc = ftrace_list->num_args;
		while (argc--)
		{
			int bytes_read = trace_buffer_read(filep, (buff + total_bytes_read), sizeof(u64));
			if (bytes_read != sizeof(u64)) {
				// if syscall is found, then it is important, that all its arguments be present
				return -EINVAL;
			}
			total_bytes_read += bytes_read;
		}

		if (ftrace_list->capture_backtrace) {
			char temp_buff[sizeof(u64)];
			int data_read = 0;

			// cannot use trace_buffer_read because os_space array

			// reading first 8 bytes (address of the function)
			while (data_read < sizeof(u64) && filep->trace_buffer->DATA_REM_READ) {
				temp_buff[data_read] = filep->trace_buffer->trace_memory[filep->trace_buffer->READ_OFFSET];
				data_read++;
				filep->trace_buffer->DATA_REM_READ -= 1;
				filep->trace_buffer->DATA_REM_WRITE += 1;
				filep->trace_buffer->READ_OFFSET = (filep->trace_buffer->READ_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
			}

			if (data_read != sizeof(u64)) {
				return -EINVAL;
			}

			// reading to be done until END_ADDR as return address encountered
			while(*(u64*)temp_buff != END_ADDR) {
				data_read = 0;
				for (int j = 0; j < sizeof(u64); j++) {
					buff[total_bytes_read + j] = temp_buff[j];
				}

				while (data_read < sizeof(u64) && filep->trace_buffer->DATA_REM_READ) {
					temp_buff[data_read] = filep->trace_buffer->trace_memory[filep->trace_buffer->READ_OFFSET];
					data_read++;
					filep->trace_buffer->DATA_REM_READ -= 1;
					filep->trace_buffer->DATA_REM_WRITE += 1;
					filep->trace_buffer->READ_OFFSET = (filep->trace_buffer->READ_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
				}
				total_bytes_read += data_read;

				if (data_read != sizeof(u64)) {
					return -EINVAL;
				}
			}
		} 

		ftrace_list = ft_md_base->next;
	}

    return total_bytes_read;
}


