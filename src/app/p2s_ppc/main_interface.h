#ifndef P2S_PPC_MAIN_INTERFACE_H__
#define P2S_PPC_MAIN_INTERFACE_H__

#ifdef __cplusplus
extern "C" {
#endif

	/************************************************************************/
	//��������
	//�����в���:		--help         ��ӡhelp
	//				--port=port    ָ��http������ַ��Ĭ��Ϊ9906
	//				--delay=ms     ����delay��Ĭ��Ϊ3500
	/************************************************************************/
	int start_service(int argc, char* argv[]);


	/************************************************************************/
	//�رշ���
	/************************************************************************/
	int stop_service(void);

#ifdef __cplusplus
}
#endif

#endif//P2S_PPC_MAIN_INTERFACE_H__
