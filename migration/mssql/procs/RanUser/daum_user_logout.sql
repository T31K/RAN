

-- daum_user_logout
CREATE Procedure [dbo].[daum_user_logout]
	/* Param List */
	@userId	char(20),
	@usernum int,
	@gametime int,
              @chanum   int,
	@svrgrp     int,
              @svrnum   int
AS
	SET NOCOUNT ON

	DECLARE 
		-- Declare variables used in error checking.
		@error_var int, 
		@rowcount_var int
	
	-- 荤侩场, 付瘤阜 肺弊牢矫埃 技泼
	UPDATE DaumUserInfo 
	SET UserLoginState=0, LastLoginDate=getdate() 
	WHERE UserUID = @userId
	
	-- 立加肺弊 巢扁扁 1 : 肺弊牢 0 : 肺弊酒眶
	INSERT INTO DaumLogLogin (UserNum, UserUID, LogInOut) 
	VALUES (@usernum, @userId, 0)
	
	-- 荤侩矫埃 巢扁扁 
	INSERT INTO DaumLogGameTime (UserNum, UserUID, GameTime, ChaNum, SGNum, SvrNum) 
	VALUES (@usernum, @userId, @gametime, @chanum, @svrgrp, @svrnum)	
	
	-- 荤侩矫埃 烹拌 诀单捞飘
	UPDATE StatGameTime SET GTime=GTime+@gametime 
	WHERE GYear=Year(GetDate()) AND GMonth=Month(GetDate()) AND GDay=Day(GetDate())
	
	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
		INSERT INTO StatGameTime (GYear, GMonth, GDay, GTime)  
		VALUES (Year(GetDate()), Month(GetDate()), Day(GetDate()), @gametime)
	END

	SET NOCOUNT OFF
