

-- daum_user_verify
CREATE PROCEDURE [dbo].[daum_user_verify]
	@userGID	char(20),
	@userUID 	char(10),
    @userSSNHEAD char(10),
    @userSEX    char(1),
	@userIp		char(20),
	@SvrGrpNum	int,
	@SvrNum	    int,
	@nReturn 	int	OUTPUT
AS	
	-- 0 ID / PWD 阂老摹
	-- 1 ID / PWD 老摹	
	-- 2 ID / PWD 啊 老摹窍绊, IP 啊 荤侩啊瓷       
    -- 3 ID / PWD 啊 老摹窍绊, IP 沥焊绰 绝澜
    -- 4 ID / PWD 啊 老摹窍绊, IP 啊 荤侩阂啊瓷 惑怕
    -- 5 ID / PWD 啊 老摹窍瘤父 捞固 立加登绢 乐澜, 吝汗立加
	-- 6 ID 喉废 惑怕
    -- 7 DAUM : 吝汗等 蜡历啊 粮犁钦聪促. 蜡历 火涝角菩
    
	DECLARE @nAvailable	int,
       	 @nUserNum	int,
		 @nState int,
		-- Declare variables used in error checking.
		 @error_var int, 
		 @rowcount_var int,
               @nBlock int,
		 @BlockDate datetime 

	SET NOCOUNT ON

	SET @nReturn = 0
	SET @nUserNum = 0
	SET @nState = 0

       -- 荤侩磊啊 粮犁窍绰瘤 眉农	
	SELECT @nUserNum=DaumUserInfo.UserNum, @nState=DaumUserInfo.UserLoginState, @nAvailable=DaumUserInfo.UserAvailable,
                    @nBlock=DaumUserInfo.UserBlock, @BlockDate=DaumUserInfo.UserBlockDate 
	FROM DaumUserInfo 
	WHERE UserUID=@userUID

       -----------------------------------------------------------------
	IF @nUserNum > 0 -- UID 啊 乐澜
	BEGIN 
		SET @nReturn = 1
		-- 5 吝汗立加, 捞固 立加登绢 乐澜
		IF @nState = 1
		BEGIN
			SET @nReturn = 5 
			RETURN @nReturn
		END
              -- 6 荤侩磊啊 荤侩阂啊瓷 惑怕
              IF @nAvailable=0
              BEGIN
                     SET @nReturn = 6
                     RETURN @nReturn
              END              
	END
	ELSE --UID 啊 绝澜
	BEGIN 		
		-- UID 啊 绝澜, 货肺款 蜡历 火涝
        INSERT INTO DaumUserInfo (UserUID, UserGID, SSNHEAD, SEX, UserType, UserLoginState, UserAvailable, SGNum, SvrNum) 
        VALUES (@userUID, @userGID, @userSSNHEAD, @userSEX, 1, 0, 1, @SvrGrpNum, @SvrNum)              
            
        SELECT @error_var = @@ERROR
		IF @error_var <> 0 
		BEGIN
            -- 蜡历 火涝吝 俊矾惯积
		    SET @nReturn = 7
            RETURN @nReturn
		END
        ELSE
        BEGIN
             -- 沥惑利栏肺 蜡历 火涝 己傍
             SELECT @nUserNum=@@IDENTITY 
             IF @nUserNum IS NULL
             BEGIN
             -- 蜡历 火涝吝 俊矾惯积
				SET @nReturn = 7
				RETURN @nReturn
             END
             
             SET @nBlock=0
             SET @nReturn = 1
         END              
	END

    -----------------------------------------------------------------
	-- IP Address 眉农
    -- IP 沥焊 乐澜
	IF (SELECT COUNT(*) FROM IPInfo WHERE ipAddress = @userIp) > 0 	
	BEGIN 
		SELECT @nAvailable=UseAvailable FROM IPInfo WHERE ipAddress = @userIp
		IF @nAvailable = 1 
		BEGIN			
			SET @nReturn = 2 -- UID 啊 沥犬窍绊 IP 啊 荤侩啊瓷
		END
		ELSE
		BEGIN			
			SET @nReturn = 4 -- UID 绰 沥犬窍瘤父 IP 啊 荤侩阂啊瓷 惑怕
		END
	END
       -- IP 沥焊 绝澜
	ELSE	
	BEGIN
		SET @nReturn = 3	 -- ID/PWD 啊 老摹窍绊, IP 沥焊绰 绝澜
	END
       
    -----------------------------------------------------------------
    -- Block 咯何 魄窜
    IF ( @nBlock = 1)
    BEGIN
		IF (@BlockDate > GetDate())
		BEGIN
			SET @nReturn = 6
		END
        ELSE
		BEGIN
			UPDATE DaumUserInfo SET UserBlock=0 WHERE UserNum = @nUserNum
            SET @nReturn  = 2
		END 
	END

	-----------------------------------------------------------------
	-- 荤侩磊 荤侩矫埃 眉农	
	IF (@nReturn = 1) OR (@nReturn = 2) OR (@nReturn = 4) OR (@nReturn = 3) 
	BEGIN
		-- 荤侩吝, 付瘤阜 肺弊牢矫埃 技泼
		UPDATE DaumUserInfo 
		SET UserLoginState=1, LastLoginDate=getdate(), SGNum=@SvrGrpNum, SvrNum=@SvrNum 
		WHERE UserNum = @nUserNum
		
		-- 立加肺弊 巢扁扁
		INSERT INTO DaumLogLogin (UserNum, UserUID, LogInOut, LogIpAddress) VALUES (@nUserNum, @userUID, 1, @userIp)	
		
		-- 烹拌 诀单捞飘
		UPDATE StatLogin SET LCount = LCount+1 WHERE LYear=Year(GetDate()) AND LMonth=Month(GetDate()) AND LDay=Day(GetDate()) AND LHour=DatePart(hour, GetDate())
		SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
		IF @error_var <> 0 OR @rowcount_var = 0
		BEGIN
			INSERT INTO StatLogin (LYEAR)  VALUES (YEAR(GetDate()))
		END
	END
	
	SET NOCOUNT OFF	
	
	RETURN @nReturn
