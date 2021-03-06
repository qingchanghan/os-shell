#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/termios.h>

#include "global.h"
//#define DEBUG

int goon = 0, ingnore = 0;       //用于设置signal信号量
char *envPath[10], cmdBuff[40];  //外部命令的存放路径及读取外部命令的缓冲空间
History history;                 //历史命令
Job *head = NULL;                //作业头指针
pid_t fgPid;                     //当前前台作业的进程号

/*******************************************************
                  工具以及辅助方法
********************************************************/
/*判断命令是否存在*/
int exists(char *cmdFile){
    int i = 0;
    if((cmdFile[0] == '/' || cmdFile[0] == '.') && access(cmdFile, F_OK) == 0){ //命令在当前目录
        strcpy(cmdBuff, cmdFile);
        return 1;
    }else{  //查找ysh.conf文件中指定的目录，确定命令是否存在
        while(envPath[i] != NULL){ //查找路径已在初始化时设置在envPath[i]中
            strcpy(cmdBuff, envPath[i]);
            strcat(cmdBuff, cmdFile);
            
            if(access(cmdBuff, F_OK) == 0){ //命令文件被找到
                return 1;
            }
            
            i++;
        }
    }
    
    return 0; 
}

/*将字符串转换为整型的Pid*/
int str2Pid(char *str, int start, int end){
    int i, j;
    char chs[20];
    
    for(i = start, j= 0; i < end; i++, j++){
        if(str[i] < '0' || str[i] > '9'){
            return -1;
        }else{
            chs[j] = str[i];
        }
    }
    chs[j] = '\0';
    
    return atoi(chs);
}

/*调整部分外部命令的格式*/
void justArgs(char *str){
    int i, j, len;
    len = strlen(str);
    
    for(i = 0, j = -1; i < len; i++){
        if(str[i] == '/'){
            j = i;
        }
    }

    if(j != -1){ //找到符号'/'
        for(i = 0, j++; j < len; i++, j++){
            str[i] = str[j];
        }
        str[i] = '\0';
    }
}

/*设置goon*/
void setGoon(){
    goon = 1;
}

/*释放环境变量空间*/
void release(){
    int i;
    for(i = 0; strlen(envPath[i]) > 0; i++){
        free(envPath[i]);
    }
}

/*******************************************************
                  信号以及jobs相关
********************************************************/
/*添加新的作业*/
Job* addJob(pid_t pid){
    Job *now = NULL, *last = NULL, *job = (Job*)malloc(sizeof(Job));
    
	//初始化新的job
    job->pid = pid;
    strcpy(job->cmd, inputBuff);
    strcpy(job->state, RUNNING);
    job->next = NULL;
    
    if(head == NULL){ //若是第一个job，则设置为头指针
        head = job;
    }else{ //否则，根据pid将新的job插入到链表的合适位置
		now = head;
		while(now != NULL && now->pid < pid){
			last = now;
			now = now->next;
		}
        last->next = job;
        job->next = now;
    }
    
    return job;
}

/*移除一个作业*/
void rmJob(int sig, siginfo_t *sip, void* noused){
    pid_t pid;
    Job *now = NULL, *last = NULL;
    
    if(ingnore == 1){
        ingnore = 0;
        return;
    }
    
    pid = sip->si_pid;

    now = head;
	while(now != NULL && now->pid < pid){
		last = now;
		now = now->next;
	}
    
    if(now == NULL){ //作业不存在，则不进行处理直接返回
        return;
    }
    
	//开始移除该作业
    if(now == head){
        head = now->next;
    }else{
        last->next = now->next;
    }
    
    free(now);
}

/*组合键命令ctrl+z*/
void ctrl_Z(){
    Job *now = NULL;
    printf("pid:%d catched ctrl_z\n",getpid());
    if(fgPid == 0){ //前台没有作业则直接返回
        return;
    }
    //SIGCHLD信号产生自ctrl+z
    ingnore = 1;
    
	now = head;
	while(now != NULL && now->pid != fgPid)
		now = now->next;
    
    if(now == NULL){ //未找到前台作业，则根据fgPid添加前台作业
        now = addJob(fgPid);
    }
    
	//修改前台作业的状态及相应的命令格式，并打印提示信息
    strcpy(now->state, STOPPED); 
    now->cmd[strlen(now->cmd)] = '&';
    now->cmd[strlen(now->cmd) + 1] = '\0';
    printf("[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd);
    

	//发送SIGSTOP信号给正在前台运作的工作，将其停止
    kill(fgPid, SIGSTOP);
    fgPid = 0;
}

/*组合键命令ctrl_C */
void ctrl_C(){
	Job *now = NULL;
    if(fgPid == 0){ //前台没有作业则直接返回
        return;
    }
//    printf("pid:%d catched ctrl_c\n",getpid());
    //SIGCHLD信号产生自ctrl+z
    ingnore = 1;
    
	now = head;
	while(now != NULL && now->pid != fgPid)
		now = now->next;
    
    if(now == NULL){ //未找到前台作业，则根据fgPid添加前台作业
        now = addJob(fgPid);
    }
    
	//修改前台作业的状态
    strcpy(now->state, DONE); 

	//发送SIGINT信号给正在前台运作的工作，将其停止
    kill(fgPid, SIGINT);
    fgPid = 0;
}

/*组合键ctrl \  */
void ctrl_Quit(){
	Job *now = NULL;
    if(fgPid == 0){ //前台没有作业则直接返回
        return;
    }
//    printf("pid:%d catched ctrl_c\n",getpid());
    //SIGCHLD信号产生自ctrl+z
    ingnore = 1;
    
	now = head;
	while(now != NULL && now->pid != fgPid)
		now = now->next;
    
    if(now == NULL){ //未找到前台作业，则根据fgPid添加前台作业
        now = addJob(fgPid);
    }
    
	//修改前台作业的状态及相应的命令格式，并打印提示信息
    strcpy(now->state, DONE); 

	//发送SIGINT信号给正在前台运作的工作，将其停止
    kill(fgPid, SIGQUIT);
    fgPid = 0;
}

/*fg命令*/
void fg_exec(int pid){    
    Job *now = NULL; 
	int i;
    pid_t spid;
    //SIGCHLD信号产生自此函数
    ingnore = 1;
    
	//根据pid查找作业
    now = head;
	while(now != NULL && now->pid != pid)
		now = now->next;
    
    if(now == NULL){ //未找到作业
        printf("pid为7%d 的作业不存在！\n", pid);
        return;
    }

    //记录前台作业的pid，修改对应作业状态
    fgPid = now->pid;
    strcpy(now->state, RUNNING);
    
/*    spid = tcgetsid(STDIN_FILENO);//记录当前的前台进程组id*/
/*    signal(SIGTTOU,SIG_IGN);//处理/忽略SIGTTOU信号,该信号由后台进程产生*/
/*    if(tcsetpgrp(STDIN_FILENO,fgPid) == -1){//*/
/*    	perror("tcsetgrp");*/
/*    }*/
/*    */
	signal(SIGTTOU,SIG_IGN);
	if(tcsetpgrp(STDIN_FILENO,fgPid) == -1){
		perror("tcsetpgrp");
	}
	
    signal(SIGTSTP, ctrl_Z); //设置signal信号，为下一次按下组合键Ctrl+Z做准备
    i = strlen(now->cmd) - 1;
    while(i >= 0 && now->cmd[i] != '&')
		i--;
    now->cmd[i] = '\0';
    
    printf("%s\n", now->cmd);
    kill(now->pid, SIGCONT); //向对象作业发送SIGCONT信号，使其运行
    int n=0;
//    printf("sent SIGCONT success.\n");//
	//waitpid(fgPid, NULL, 0); //父进程等待前台进程的运行
    while(waitpid(fgPid, NULL, WUNTRACED) == -1);
    
	tcsetpgrp(STDIN_FILENO,getpid());
    
//    printf("n:%d\n",n);
    
}

/*bg命令*/
void bg_exec(int pid){
    Job *now = NULL;
    
    //SIGCHLD信号产生自此函数
    ingnore = 1;
    
	//根据pid查找作业
	now = head;
    while(now != NULL && now->pid != pid)
		now = now->next;
    
    if(now == NULL){ //未找到作业
        printf("pid为7%d 的作业不存在！\n", pid);
        return;
    }
    strcpy(now->state, RUNNING); //修改对象作业的状态
    printf("[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd);
    kill(now->pid, SIGCONT); //向对象作业发送SIGCONT信号，使其运行
}

/*******************************************************
                    命令历史记录
********************************************************/
void addHistory(char *cmd){
    if(history.end == -1){ //第一次使用history命令
        history.end = 0;
        strcpy(history.cmds[history.end], cmd);
        return;
	}
    
    history.end = (history.end + 1)%HISTORY_LEN; //end前移一位
    strcpy(history.cmds[history.end], cmd); //将命令拷贝到end指向的数组中
    
    if(history.end == history.start){ //end和start指向同一位置
        history.start = (history.start + 1)%HISTORY_LEN; //start前移一位
    }
}

/*******************************************************
                     初始化环境
********************************************************/
/*通过路径文件获取环境路径*/
void getEnvPath(int len, char *buf){
    int i, j, last = 0, pathIndex = 0, temp;
    char path[40];
    
    for(i = 0, j = 0; i < len; i++){
        if(buf[i] == ':'){ //将以冒号(:)分隔的查找路径分别设置到envPath[]中
            if(path[j-1] != '/'){
                path[j++] = '/';
            }
            path[j] = '\0';
            j = 0;
            
            temp = strlen(path);
            envPath[pathIndex] = (char*)malloc(sizeof(char) * (temp + 1));
            strcpy(envPath[pathIndex], path);
            
            pathIndex++;
        }else{
            path[j++] = buf[i];
        }
    }
    
    envPath[pathIndex] = NULL;
}

/*初始化操作*/
void init(){
    int fd, n, len;
    char c, buf[80];

	//打开查找路径文件ysh.conf
    if((fd = open("ysh.conf", O_RDONLY, 660)) == -1){
        perror("init environment failed\n");
        exit(1);
    }
    
	//初始化history链表
    history.end = -1;
    history.start = 0;
    
    len = 0;
	//将路径文件内容依次读入到buf[]中
    while(read(fd, &c, 1) != 0){
        buf[len++] = c;
    }
    buf[len] = '\0';

    //将环境路径存入envPath[]
    getEnvPath(len, buf); 
    
    //注册信号
    struct sigaction action;
    action.sa_sigaction = rmJob;
    sigfillset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGCHLD, &action, NULL);
    signal(SIGTSTP, ctrl_Z);
    signal(SIGINT,ctrl_C);
    signal(SIGQUIT,ctrl_Quit);
}

/*******************************************************
                      命令解析
********************************************************/
SimpleCmd* handleSimpleCmdStr(int begin, int end){
    int i, j, k;
    int fileFinished; //记录命令是否解析完毕
    char c, buff[10][40], inputFile[30], outputFile[30], *temp = NULL;
    SimpleCmd *cmd = (SimpleCmd*)malloc(sizeof(SimpleCmd));
    
	//默认为非后台命令，输入输出重定向为null
    cmd->isBack = 0;
    cmd->input = cmd->output = NULL;
    
    //初始化相应变量
    for(i = begin; i<10; i++){
        buff[i][0] = '\0';
    }
    inputFile[0] = '\0';
    outputFile[0] = '\0';
    
    i = begin;
	//跳过空格等无用信息
    while(i < end && (inputBuff[i] == ' ' || inputBuff[i] == '\t')){
        i++;
    }
    
    k = 0;
    j = 0;
    fileFinished = 0;
    temp = buff[k]; //以下通过temp指针的移动实现对buff[i]的顺次赋值过程
    while(i < end){
		/*根据命令字符的不同情况进行不同的处理*/
        switch(inputBuff[i]){
            case ' ':
            case '\t': //命令名及参数的结束标志
                temp[j] = '\0';
                j = 0;
                if(!fileFinished){
                    k++;
                    temp = buff[k];
                }
                break;

            case '<': //输入重定向标志
                if(j != 0){
		    //此判断为防止命令直接挨着<符号导致判断为同一个参数，如果ls<sth
                    temp[j] = '\0';
                    j = 0;
                    if(!fileFinished){
                        k++;
                        temp = buff[k];
                    }
                }
                temp = inputFile;
                fileFinished = 1;
                i++;
                break;
                
            case '>': //输出重定向标志
                if(j != 0){
                    temp[j] = '\0';
                    j = 0;
                    if(!fileFinished){
                        k++;
                        temp = buff[k];
                    }
                }
                temp = outputFile;
                fileFinished = 1;
                i++;
                break;
                
            case '&': //后台运行标志
                if(j != 0){
                    temp[j] = '\0';
                    j = 0;
                    if(!fileFinished){
                        k++;
                        temp = buff[k];
                    }
                }
                cmd->isBack = 1;
                fileFinished = 1;
                i++;
                break;
                
            default: //默认则读入到temp指定的空间
                temp[j++] = inputBuff[i++];
                continue;
		}
        
		//跳过空格等无用信息
        while(i < end && (inputBuff[i] == ' ' || inputBuff[i] == '\t')){
            i++;
        }
	}
    
    if(inputBuff[end-1] != ' ' && inputBuff[end-1] != '\t' && inputBuff[end-1] != '&'){
        temp[j] = '\0';
        if(!fileFinished){
            k++;
        }
    }
    
	//依次为命令名及其各个参数赋值
    cmd->args = (char**)malloc(sizeof(char*) * (k + 1));
    cmd->args[k] = NULL;
    for(i = 0; i<k; i++){
        j = strlen(buff[i]);
        cmd->args[i] = (char*)malloc(sizeof(char) * (j + 1));   
        strcpy(cmd->args[i], buff[i]);
    }
    
	//如果有输入重定向文件，则为命令的输入重定向变量赋值
    if(strlen(inputFile) != 0){
        j = strlen(inputFile);
        cmd->input = (char*)malloc(sizeof(char) * (j + 1));
        strcpy(cmd->input, inputFile);
    }

    //如果有输出重定向文件，则为命令的输出重定向变量赋值
    if(strlen(outputFile) != 0){
        j = strlen(outputFile);
        cmd->output = (char*)malloc(sizeof(char) * (j + 1));   
        strcpy(cmd->output, outputFile);
    }
    #ifdef DEBUG
    printf("****\n");
    printf("isBack: %d\n",cmd->isBack);
    	for(i = 0; cmd->args[i] != NULL; i++){
    		printf("args[%d]: %s\n",i,cmd->args[i]);
	}
    printf("input: %s\n",cmd->input);
    printf("output: %s\n",cmd->output);
    printf("****\n");
    #endif
    return cmd;
}

/*******************************************************
                      命令执行
********************************************************/
/*执行外部命令*/
void execOuterCmd(SimpleCmd *cmd){
    pid_t pid;
    int pipeIn, pipeOut;
    if(exists(cmd->args[0])){ //命令存在

        if((pid = fork()) < 0){
            perror("fork failed");
            return;
        }
        if(pid == 0){ //子进程
//			printf("Child process begin!\n");
            if(cmd->input != NULL){ //存在输入重定向
                if((pipeIn = open(cmd->input, O_RDONLY, S_IRUSR|S_IWUSR)) == -1){
                    printf("不能打开文件 %s！\n", cmd->input);
                    return;
                }
                if(dup2(pipeIn, 0) == -1){
                    printf("重定向标准输入错误！\n");
                    return;
                }
            }
            
	        if(cmd->output != NULL){ //存在输出重定向
	            if((pipeOut = open(cmd->output, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1){
	                printf("不能打开文件 %s！\n", cmd->output);
	                return ;
	            }
	            if(dup2(pipeOut, 1) == -1){
	                printf("重定向标准输出错误！\n");
	                return;
	            }
	        }
            
            if(cmd->isBack){ //若是后台运行命令，等待父进程增加作业
                kill(getppid(),SIGUSR1);//add a statement
				signal(SIGUSR1, setGoon); //收到信号，setGoon函数将goon置1，以跳出下面的循环
                while(goon == 0);
/*				{*/
/*					sleep(1);					*/
/*					printf("wait father process...\n");*/
/*				}; //等待父进程SIGUSR1信号，表示作业已加到链表中*/
                goon = 0; //置0，为下一命令做准备
                
                printf("[%d]\t%s\t\t%s\n", getpid(), RUNNING, inputBuff);
                //kill(getppid(), SIGUSR1);
//				printf("Send a signal to father process!\n");
            }
            else{
            	
            }
            
            justArgs(cmd->args[0]);
            signal(SIGTSTP, ctrl_Z);
            if(execv(cmdBuff, cmd->args) < 0){ //执行命令
                printf("execv failed!\n");
                return;
            }

        }
		else{ //父进程
            signal(SIGUSR1, setGoon);
//			printf("father process begin!\n");
           	setpgid(pid, 0);//设置子进程的组ID,使其不接受前台命令的信号,0表示由当前的pid设置组id,先set后转移session控制权
           	
            if(cmd ->isBack){ //后台命令
/*                fgPid = 0; //pid置0，为下一命令做准备*/
/*                addJob(pid); //增加新的作业*/
                //kill(pid, SIGUSR1); //子进程发信号，表示作业已加入
                
                //等待子进程输出
                while(goon == 0);
/*				{*/
/*					sleep(1);*/
/*					printf("wait child process...\n");*/
/*				};*/
                goon = 0;
                
                fgPid = 0;
                addJob(pid);
//				printf("father process ok\n");
				kill(pid, SIGUSR1);//add a statement
            }else{ //非后台命令
            	pid_t spid;
                fgPid = pid;
                int m=0;
                
                spid = tcgetsid(STDIN_FILENO);	
                printf("spid:%d pid:%d\n",spid,getpid());
                signal(SIGTTOU,SIG_IGN);
//                printf("wait1:%s\n",cmd->args[0]);
				if(tcsetpgrp(STDIN_FILENO, pid) == -1){
					printf("tcsetpgrp failed.\n");
				}
                while(m = waitpid(pid, NULL, WUNTRACED) == -1){
                	continue;
                };
//                printf("errno:%d\n",errno);
//                printf("ok1:%s\n",cmd->args[0]);
                tcsetpgrp(STDIN_FILENO,getpid());
                
//				printf("child:%d,father:%d\n",m,getpid());
            }
		}
    }else{ //命令不存在
        printf("找不到命令 15%s\n", inputBuff);
    }
}

/*执行命令*/
void execSimpleCmd(SimpleCmd *cmd){
    int i, pid, isOutCmd;
    char *temp;
    Job *now = NULL;
    int pipeIn, pipeOut,in,out;
	if(cmd->input != NULL){ //存在输入重定向
		in = dup(STDIN_FILENO);
		if((pipeIn = open(cmd->input, O_RDONLY, S_IRUSR|S_IWUSR)) == -1){
		    printf("不能打开文件 %s！\n", cmd->input);
		    return;
		}
		if(dup2(pipeIn, 0) == -1){
		    printf("重定向标准输入错误！\n");
		    return;
		}
	}

	if(cmd->output != NULL){ //存在输出重定向
		out = dup(STDOUT_FILENO);
		if((pipeOut = open(cmd->output, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1){
		    printf("不能打开文件 %s！\n", cmd->output);
		    return ;
		}
		if(dup2(pipeOut, 1) == -1){
		    printf("重定向标准输出错误！\n");
		    return;
		}
	}
	isOutCmd = 0;
    if(strcmp(cmd->args[0], "exit") == 0) { //exit命令
        exit(0);
    } else if (strcmp(cmd->args[0], "history") == 0) { //history命令
        if(history.end == -1){
            printf("尚未执行任何命令\n");
            return;
        }
        i = history.start;
        do {
            printf("%s\n", history.cmds[i]);
            i = (i + 1)%HISTORY_LEN;
        } while(i != (history.end + 1)%HISTORY_LEN);
    } else if (strcmp(cmd->args[0], "jobs") == 0) { //jobs命令
        if(head == NULL){
            printf("尚无任何作业\n");
        } else {
            printf("index\tpid\tstate\t\tcommand\n");
            for(i = 1, now = head; now != NULL; now = now->next, i++){
                printf("%d\t%d\t%s\t\t%s\n", i, now->pid, now->state, now->cmd);
            }
        }
    } else if (strcmp(cmd->args[0], "cd") == 0) { //cd命令
        temp = cmd->args[1];
        if(temp != NULL){
            if(chdir(temp) < 0){
                printf("cd; %s 错误的文件名或文件夹名！\n", temp);
            }
        }
    } else if (strcmp(cmd->args[0], "fg") == 0) { //fg命令
        temp = cmd->args[1];
        if(temp != NULL && temp[0] == '%'){
            pid = str2Pid(temp, 1, strlen(temp));
            if(pid != -1){
                fg_exec(pid);
            }
        }else{
            printf("fg; 参数不合法，正确格式为：fg %%<int>\n");
        }
    } else if (strcmp(cmd->args[0], "bg") == 0) { //bg命令
        temp = cmd->args[1];
        if(temp != NULL && temp[0] == '%'){
            pid = str2Pid(temp, 1, strlen(temp));
            
            if(pid != -1){
                bg_exec(pid);
            }
        }
		else{
            printf("bg; 参数不合法，正确格式为：bg %%<int>\n");
        }
    }
/*    else if (strcmp(cmd->args[0], "echo") == 0){ //echo命令*/
/*    	i = 1;*/
/*    	if(strcmp(cmd->args[i],"-n")){//无-n参数*/
/*    		for(i=1;cmd->args[i]!=NULL;i++){*/
/*    			if(i==1){*/
/*    				printf("%s",cmd->args[i]);*/
/*    			}*/
/*    			else{*/
/*    				printf(" %s",cmd->args[i]);*/
/*    			}*/
/*    		}*/
/*    		printf("\n");*/
/*    	}*/
/*    	else{*/
/*    		for(i=2;cmd->args[i]!=NULL;i++){//有-n参数*/
/*    			if(i==2){*/
/*    				printf("%s",cmd->args[i]);*/
/*    			}*/
/*    			else {*/
/*    				printf(" %s",cmd->args[i]);*/
/*    			}*/
/*    		}*/
/*    	}*/
/*    }*/
    else if(strcmp(cmd->args[0],"pwd") == 0){//pwd命令
    	printf("%s\n",get_current_dir_name());
    } 
    else if (strcmp(cmd->args[0], "type") == 0){ //type 命令
    	for(i = 1;cmd->args[i] != NULL;i++){
       		if( strcmp(cmd->args[i], "exit") == 0 || strcmp(cmd->args[i], "history") == 0 || strcmp(cmd->args[i], "fg") == 0||
       			strcmp(cmd->args[i], "bg") == 0 || strcmp(cmd->args[i], "jobs") ==0 ||  
       			strcmp(cmd->args[i], "type") == 0 || strcmp(cmd->args[i], "cd") == 0|| strcmp(cmd->args[i], "pwd") == 0){
       			printf("%s is a shell builtin\n",cmd->args[i]);
       		}
       		else if(exists(cmd->args[i])){
       			printf("%s is %s\n",cmd->args[i],cmdBuff);
       		}
       		else {
       			printf("user-sh: type: %s: not found\n",cmd->args[i]);
       		}
    	}

    }
    else{ //外部命令
    	isOutCmd = 1;
    }
    
    if(cmd->input != NULL){ //存在输入重定向
		if(dup2(in, STDIN_FILENO) == -1){
		    printf("重定向标准输入错误！\n");
		    return;
		}
	}

	if(cmd->output != NULL){ //存在输出重定向
		if(dup2(out, STDOUT_FILENO) == -1){
		    printf("重定向标准输出错误！\n");
		    return;
		}
	}
	
    if(isOutCmd){//外部命令
	    execOuterCmd(cmd);
    }
    
    //释放结构体空间
    for(i = 0; cmd->args[i] != NULL; i++){
        free(cmd->args[i]);
        free(cmd->input);
        free(cmd->output);
    }
}

/*******************************************************
                     命令执行接口
********************************************************/
void execute(){
	pid_t pid;
	int fd[2];
	int isPipe,in,out,i;
	isPipe = 0;
	for(i=strlen(inputBuff);i>0;i--){
		if(inputBuff[i] == '|'){
			isPipe = 1;
			pipe(fd);
			pid = fork();
			if(pid == -1){
				perror("fork failed");
				exit(1);
			}
			else if(pid == 0){//子进程
                
				out = dup(STDOUT_FILENO);
				inputBuff[i]='\0';
//				printf("cmd:%s\n",inputBuff);
				dup2(fd[1],1);
				close(fd[0]);
				
				execute();
    			dup2(out,STDOUT_FILENO);
			
    			exit(0);
			}
			else{//父进程
			
				in = dup(STDIN_FILENO);
				strcpy(inputBuff,inputBuff+i+1);
//				printf("cmd:%s\n",inputBuff);
				dup2(fd[0],0);
				close(fd[1]);
				SimpleCmd *cmd = handleSimpleCmdStr(0, strlen(inputBuff));
    			execSimpleCmd(cmd);
    			dup2(in,STDIN_FILENO);
    			
    			while(waitpid(pid,NULL,WUNTRACED) == -1)continue;
//    			printf("father process finished%d\n");
			}
			break;
		}
	}
	if(!isPipe){
    	SimpleCmd *cmd = handleSimpleCmdStr(0, strlen(inputBuff));
    	execSimpleCmd(cmd);
    }
}
