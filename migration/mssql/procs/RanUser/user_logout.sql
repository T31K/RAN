CREATE Procedure [dbo].[user_logout]
	@userId	char(25),
	@usernum int,
	@gametime int,
	@chanum   int,
	@svrgrp     int,
	@svrnum   int,
	@totalgametime	int,
	@offlinetime	int
AS
	SET NOCOUNT ON

	DECLARE 
		-- Declare variables used in error checking.
		@error_var int, 
		@rowcount_var int
	
	-- 荤侩场, 付瘤阜 肺弊牢矫埃 技泼
	UPDATE UserInfo
	SET UserLoginState=0, LastLoginDate=getdate(), GameTime=@totalgametime, OfflineTime=@offlinetime
	WHERE UserNum = @usernum
	
	-- 立加肺弊 巢扁扁 1 : 肺弊牢 0 : 肺弊酒眶
	INSERT INTO LogLogin (UserNum, UserID, LogInOut) 
	VALUES (@usernum, @userId, 0)
	
	-- 荤侩矫埃 巢扁扁 
	INSERT INTO LogGameTime (UserNum, UserID, GameTime, ChaNum, SGNum, SvrNum) 
	VALUES (@usernum, @userId, @gametime, @chanum, @svrgrp, @svrnum)

	UPDATE UserInfo
    SET GameTime2=GameTime2+@gametime 
    WHERE usernum = @usernum

    -- 荤侩矫埃 烹拌 诀单捞飘
	UPDATE StatGameTime
	SET GTime=GTime+@gametime 
	WHERE GYear=Year(GetDate()) AND GMonth=Month(GetDate()) AND GDay=Day(GetDate())

	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
		INSERT INTO StatGameTime (GYear, GMonth, GDay, GTime)  
		VALUES (Year(GetDate()), Month(GetDate()), Day(GetDate()), @gametime)
	END

	SET NOCOUNT OFF

