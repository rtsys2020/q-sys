#include "System.h"

#define UA_Debug Debug

extern const u8 gPageTotal;

extern u16 gEntriesOfPage[];//记录每个页面进入的次数
extern u32 gPagePeripEvtFlag[];//记录每个页面的外围事件响应标志
extern GOBAL_PERIPEVT_RECORD gGobalPeripEvtRecord[];//记录所有页面的全局事件表
extern u32 gGobalPeripEvtBitFlag;//全局事件标志
extern SYS_MSG gCurrSysMsg;//记录页面case返回的信息
extern u8 gPageHeapRecord;//记录用户页面堆栈分配数目的变量

extern const PAGE_ATTRIBUTE *GetPageByIdx(u8);
extern PAGE_RID GetRegIdByIdx(u8);
extern u8 GetPageIdxByTrack(u8);
extern u8 GetPageIdxByLayerOffset(u8);
extern u8 GetPageIdxByLayer(u8);
extern SYS_MSG FindPage(u8 *,u32,u8 *);

//用户可用的系统函数开始
//页面使用的内存分配函数
#if Q_HEAP_TRACK_DEBUG ==1
void *_Q_PageMallco(u16 Size,u8 *pFuncName,u32 Lines)
#else
void *_Q_PageMallco(u16 Size)
#endif
{
	gPageHeapRecord++;
#if Q_HEAP_TRACK_DEBUG ==1
	return QS_Mallco(Size,pFuncName,Lines);
#else
	return QS_Mallco(Size);
#endif
}

//页面使用的内存释放函数
#if Q_HEAP_TRACK_DEBUG ==1
void _Q_PageFree(void *Ptr,u8 *pFuncName,u32 Lines)
#else
void _Q_PageFree(void *Ptr)
#endif
{
	gPageHeapRecord--;
#if Q_HEAP_TRACK_DEBUG ==1
	QS_Free(Ptr,pFuncName,Lines);
#else
	QS_Free(Ptr);
#endif
}
extern void *KeysHandler_Task_Handle;
//开启触摸屏输入，外部按键输入
void Q_EnableInput(void)
{
	Enable_Touch_Inperrupt();
	OS_TaskResume(KeysHandler_Task_Handle);//恢复按键监控线程
}

//关闭触摸屏输入，外部按键输入
void Q_DisableInput(void)
{
	Disable_Touch_Inperrupt();
	OS_TaskSuspend(KeysHandler_Task_Handle);//挂起按键监控线程
}

//获取页面总数
u8 Q_GetPageTotal(void)
{
	return gPageTotal;
}

//指定相对当前页面的痕迹偏移值，返回页面指针
//如GetPageByTrack(0)返回当前页面指针
//Q_GetPageByTrack(1)返回前一页面指针
const PAGE_ATTRIBUTE *Q_GetPageByTrack(u8 Local)
{
	return GetPageByIdx(GetPageIdxByTrack(Local));
}

//LayerOffset=0,返回当前页面指针
//LayerOffset=1,返回上一层页面指针
const PAGE_ATTRIBUTE *Q_GetPageByLayerOffset(u8 LayerOffset)
{
	return GetPageByIdx(GetPageIdxByLayerOffset(LayerOffset));
}

//得到指定层的页面指针
//LayerNum=1,得到顶层
//LayerNum=2,得到第二层
const PAGE_ATTRIBUTE *Q_GetPageByLayer(u8 LayerNum)
{
	return GetPageByIdx(GetPageIdxByLayer(LayerNum));
}

//通过页面名称找页面的RegID
//如果入口参数为NULL则返回当前页面的RegID
PAGE_RID Q_FindRidByPageName(u8 *PageName)
{
	u8 PageIdx;

	if((PageName==NULL)||(PageName[0]==0))
	{
		return GetRegIdByIdx(GetPageIdxByTrack(0));
	}
	
	for(PageIdx=0;PageIdx<gPageTotal;PageIdx++)
	{
		if(strcmp((void *)PageName,(void *)GetPageByIdx(PageIdx)->Name)) continue;
		else  return GetRegIdByIdx(PageIdx);//找到指定的页面了
	}

	Debug("No Such Page PageName:%s ,may be this is a RID\n\r",PageName);
	return PRID_Null;
}

//获取当前页面名称
u8 *Q_GetCurrPageName(void)
{
	return Q_GetPageByTrack(0)->Name;
}

//获取当前页面进入次数
u16 Q_GetPageEntries(void)
{
	return gEntriesOfPage[GetPageIdxByTrack(0)];
}

//转向某页面,调用此函数后，InputHandler主线程在完成当前任务后，会开始转向工作。
//return TRUE :允许转向
//return FALSE : 不允许转向
//页面与页面之间的参数传递可用pInfoParam指针
SYS_MSG Q_GotoPage(PAGE_ACTION PageAction, u8 *Name, int IntParam, void *pSysParam)
{
	INPUT_EVENT InEventParam;
	u8 PageIdx;
	u8 Result;
	
	if(GetCurrPage())
		UA_Debug("%s : %s->%s\n\r",__FUNCTION__,GetCurrPage()->Name,Name);
	else
		UA_Debug("%s : NULL->%s\n\r",__FUNCTION__,Name);
	
	//查找对应页面
	if(PageAction==SubPageReturn) //如果是子页面返回，则找到上一级页面
	{
		gCurrSysMsg=SM_State_OK;
		PageIdx=GetPageIdxByLayerOffset(1);
	}
	else
	{
		gCurrSysMsg=FindPage(Name,0,&PageIdx);//初始化页面回传信息
		if(gCurrSysMsg!=SM_State_OK)//没找到对应页面
		{
			return gCurrSysMsg;
		}
	}

	if(GetPageByIdx(PageIdx)->Type==POP_PAGE)//要进入的页面是pop页面
		if((PageAction!=GotoSubPage)&&(PageAction!=SubPageReturn))
		{
			Q_ErrorStopScreen("Pop Page not allow entry by \"GotoNewPage\" & \"SubPageTranslate\" param!");
			return SM_State_Faile;
		}

	//POP页面只允许以子页面返回的形式退出
	if((Q_GetPageByTrack(0)->Type==POP_PAGE)&&(PageAction!=SubPageReturn))
	{
		Q_ErrorStopScreen("Pop Page only allow quit by \"SubPageReturn\" param!");
		return SM_State_Faile;
	}

	if(Q_GetPageByTrack(0)->Type!=POP_PAGE) //如果从pop页面返回，那么不需要执行这个goto case
		gCurrSysMsg=GetPageByIdx(PageIdx)->SysEvtHandler(Sys_PreGotoPage, IntParam,pSysParam);

	//Debug("GotoPage Return 0x%x\n\r",SysMsg);
	
	if(gCurrSysMsg&SM_NoGoto)//页面的Sys_Goto_Page传递回的信息
	{//新页面的Sys_PreGotoPage传递回SM_NoGoto信息表示不需要进入此页面了。
		return gCurrSysMsg;
	}
	else
	{
		InEventParam.uType=Sync_Type;
		switch(PageAction)
		{
			case GotoNewPage:
				InEventParam.EventType=Input_GotoNewPage;break;
			case GotoSubPage:
				InEventParam.EventType=Input_GotoSubPage;break;
			case SubPageReturn:
				InEventParam.EventType=Input_SubPageReturn;break;
		}
		InEventParam.Num=PageIdx;
		InEventParam.Info.SyncInfo.IntParam=IntParam;
		InEventParam.Info.SyncInfo.pParam=pSysParam;
		//Debug("New Page Index:%d\n\r",PageIdx);
		if((Result=OS_MsgBoxSend(gInputHandler_Queue,&InEventParam,100,FALSE))==OS_ERR_NONE)
		{
			return gCurrSysMsg;
		}
		else
		{
			Debug("GotoPage Send Msg Error!%d\n\r",Result);
			return SM_State_Faile;
		}
	}	
}

//设置系统事件对应位
void Q_SetPeripEvt(PAGE_RID RegID,u32 PeripEvtCon)
{
	u8 PageIdx;
	OS_DeclareCritical();

	if(RegID)
	{
		if(FindPage("",RegID,&PageIdx)!=SM_State_OK) //此处状态遗失�
		{
			Q_ErrorStopScreen("Can't find page!\n\r");
		}
	}
	else //如果RegID为0，返回当前页面的事件标志
	{
		PageIdx=GetPageIdxByTrack(0);
	}
	
	OS_EnterCritical();
	gPagePeripEvtFlag[PageIdx]|=PeripEvtCon;
	OS_ExitCritical();
}

//清楚系统事件对应位
void Q_ClrPeripEvt(PAGE_RID RegID,u32 PeripEvtCon)
{
	u8 PageIdx;
	OS_DeclareCritical();

	if(RegID)
	{
		if(FindPage("",RegID,&PageIdx)!=SM_State_OK) return;//此处状态遗失。
	}
	else //如果RegID为0，返回当前页面的事件标志
	{
		PageIdx=GetPageIdxByTrack(0);
	}
	
	OS_EnterCritical();
	gPagePeripEvtFlag[PageIdx]&=PeripEvtCon;
	OS_ExitCritical();
}

//打开系统事件标志
void Q_EnablePeripEvt(PAGE_RID RegID,PERIP_EVT PeripEvt)
{
	u8 PageIdx;
	OS_DeclareCritical();

	if(RegID)
	{
		if(FindPage("",RegID,&PageIdx)!=SM_State_OK) return;//此处状态遗失。
	}
	else //如果RegID为0，返回当前页面的事件标志
	{
		PageIdx=GetPageIdxByTrack(0);
	}
	
	OS_EnterCritical();
	SetBit(gPagePeripEvtFlag[PageIdx],PeripEvt);
	OS_ExitCritical();
}

//关闭系统事件标志
void Q_DisablePeripEvt(PAGE_RID RegID,PERIP_EVT PeripEvt)
{
	u8 PageIdx;
	OS_DeclareCritical();

	if(RegID)
	{
		if(FindPage("",RegID,&PageIdx)!=SM_State_OK) return;//此处状态遗失。
	}
	else //如果RegID为0，返回当前页面的事件标志
	{
		PageIdx=GetPageIdxByTrack(0);
	}
	
	OS_EnterCritical();
	ClrBit(gPagePeripEvtFlag[PageIdx],PeripEvt);
	OS_ExitCritical();
}

//查看系统事件
//如果RegID为0，返回当前页面的事件标志
INSPECT_SYSEVT_RET Q_InspectPeripEvt(PAGE_RID RegID,PERIP_EVT PeripEvt)
{
	u8 PageIdx;
	
	if(RegID)
	{
		if(FindPage("",RegID,&PageIdx)!=SM_State_OK) return NoHasSysEvt;//此处状态遗失。
	}
	else //如果RegID为0，返回当前页面的事件标志
	{
		PageIdx=GetPageIdxByTrack(0);
	}

	if(ReadBit(gPagePeripEvtFlag[PageIdx],PeripEvt)) return HasPagePeripEvt;
	if(ReadBit(gGobalPeripEvtBitFlag,PeripEvt)) return HasGobalSysEvt;
	return NoHasSysEvt;
}

//设置全局事件，任何页面下，都会触发事件的处理函数SysEventHandler
//不对Sys_PreGotoPage起作用
void Q_EnableGobalPeripEvent(PERIP_EVT PeripEvt,PeripheralsHandlerFunc PeripEvtHandler)
{
	u8 i;

	if(PeripEvtHandler==NULL) return;
	
	//找是否有重复记录
	for(i=0;i<MAX_GOBAL_SYSEVT;i++)
	{
		if((gGobalPeripEvtRecord[i].PeripEvt==PeripEvt)&&(gGobalPeripEvtRecord[i].GobalPeripEvtHandler==PeripEvtHandler))
			return;
	}	
	
	//找空位子
	for(i=0;i<MAX_GOBAL_SYSEVT;i++)
	{
		if(gGobalPeripEvtRecord[i].PeripEvt==0)break;
	}
	if(i==MAX_GOBAL_SYSEVT) 
	{
		Q_ErrorStopScreen("MAX_GOBAL_SYSEVT is small!!! pls reset it!");
	}

	gGobalPeripEvtRecord[i].PeripEvt=PeripEvt;
	gGobalPeripEvtRecord[i].GobalPeripEvtHandler=PeripEvtHandler;
	SetBit(gGobalPeripEvtBitFlag,PeripEvt);
}

//注销全局事件
void Q_DisableGobalPeripEvent(PERIP_EVT PeripEvt,PeripheralsHandlerFunc PeripEvtHandler)
{
	u8 i;

	//找到匹配记录
	for(i=0;i<MAX_GOBAL_SYSEVT;i++)
	{
		if((gGobalPeripEvtRecord[i].PeripEvt==PeripEvt)&&(gGobalPeripEvtRecord[i].GobalPeripEvtHandler==PeripEvtHandler))
		{
			gGobalPeripEvtRecord[i].PeripEvt=(PERIP_EVT)0;//清除记录
		}
	}	

	for(i=0;i<MAX_GOBAL_SYSEVT;i++) //检查是否还有其他同类全局事件
	{
		if(gGobalPeripEvtRecord[i].PeripEvt==PeripEvt) return;
	}
	ClrBit(gGobalPeripEvtBitFlag,PeripEvt);//没有就清标志
}

//用于错误停止
void Q_ErrorStop(const char *FileName,const char *pFuncName,const u32 Line,const char *Msg)
{
	u32 Gray;
	u16 Color;
	u16 x,y;
	u32 R,G,B;
	GUI_REGION DrawRegion;
	u8 ErrorMsg[256];

	Debug(Msg);

	for(y=0;y<LCD_HIGHT;y++)
		for(x=0;x<LCD_WIDTH;x++)
		{
			Color=Gui_ReadPixel16Bit(x,y);
			R=((Color&0x1f)<<3);
			G=(((Color>>5)&0x3f)<<2);
			B=(((Color>>11)&0x1f)<<3);	
			Gray =(R*38 + G*75 + B*15) >> 7;
			//if((x>20)&&(x<LCD_WIDTH-20)&&(y>20)&&(y<LCD_HIGHT-20))
				//Gray=(Gray*9)>>3;if(Gray>0xff) Gray=0xff;
			Gray =((Gray&0xf8)<<8)+((Gray&0xfc)<<3)+((Gray&0xf8)>>3);
			Gui_WritePixel(x,y,Gray);
		}	

	if(GetCurrPage())
		sprintf((void *)ErrorMsg,"!!!--SYS ERROR STOP--!!!\n\rNow Page:%s\n\rFile:%s\n\rFunction:%s()\n\rLine:%d\nMsg:%s",GetCurrPage()->Name,FileName,pFuncName,Line,Msg);
	else
		sprintf((void *)ErrorMsg,"!!!--SYS ERROR STOP--!!!\n\rFile:%s\n\rFunction:%s()\n\rLine:%d\n\rMsg:%s",FileName,pFuncName,Line,Msg);

	DrawRegion.x=DrawRegion.y=19;
	DrawRegion.w=200;
	DrawRegion.h=280;
	DrawRegion.Color=FatColor(0xffffff);
	DrawRegion.Space=0x00;
	Gui_DrawFont(ASC14B_FONT,ErrorMsg,&DrawRegion);
		
	DrawRegion.x=21;
	DrawRegion.y=19;
	Gui_DrawFont(ASC14B_FONT,ErrorMsg,&DrawRegion);

	DrawRegion.x=19;
	DrawRegion.y=21;
	Gui_DrawFont(ASC14B_FONT,ErrorMsg,&DrawRegion);

	DrawRegion.x=21;
	DrawRegion.y=21;
	Gui_DrawFont(ASC14B_FONT,ErrorMsg,&DrawRegion);

	DrawRegion.x=20;
	DrawRegion.y=20;
	DrawRegion.Color=FatColor(0xff0000);
	Gui_DrawFont(ASC14B_FONT,ErrorMsg,&DrawRegion);

	while(1);
}

//用户可用的系统函数结束

